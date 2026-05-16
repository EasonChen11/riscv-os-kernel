/* QEMU ramfb driver — adapted from ex52/video.c
 *
 * 整體流程：
 * 1. video_init 透過 fw_cfg 找到 "etc/ramfb" entry，把一份 RAMFBCfg 寫進去
 *    告訴 QEMU 「framebuffer 在 FB_BASE，寬高/格式如此」。
 * 2. 之後 user 寫 FB_BASE 那塊 DRAM，QEMU display 就會掃出來。
 * 3. OrangePi RV2 那條路（沒 ramfb，硬編 base 0x7f700000）。
 */

#include <stdint.h>
#include "video.h"
#include "string.h"
#include "pgtable.h"

#define XRGB8888  875713112        /* "XR24" → little-endian fourcc */

#define QEMU_PACKED __attribute__((packed))

// 自己 inline shift 而非 __builtin_bswap*：freestanding 環境沒 libgcc，
// builtin 在沒 -O2 inline 時會 lower 成 __bswapsi2/__bswapdi2 link 失敗。
static inline uint64_t bswap64(uint64_t x) {
    return ((x & 0xFF00000000000000ULL) >> 56) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x000000FF00000000ULL) >>  8) |
           ((x & 0x00000000FF000000ULL) <<  8) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x00000000000000FFULL) << 56);
}
static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0xFF000000U) >> 24) |
           ((x & 0x00FF0000U) >>  8) |
           ((x & 0x0000FF00U) <<  8) |
           ((x & 0x000000FFU) << 24);
}
static inline uint16_t bswap16(uint16_t x) {
    return (uint16_t)(((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8));
}

// fw_cfg 的所有結構都是 big-endian on the wire，C 端寫入前要 bswap
struct QEMU_PACKED RAMFBCfg {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

#define FW_CFG_PA     0x10100000UL
#define FW_CFG_BASE   (PAGE_OFFSET + FW_CFG_PA)
#define FW_CFG_SELECT (uint16_t*)(FW_CFG_BASE + 0x08)
#define FW_CFG_DATA   (uint64_t*)(FW_CFG_BASE + 0x00)
#define FW_CFG_DMA    (uint64_t*)(FW_CFG_BASE + 0x10)

#define FW_CFG_DMA_CTL_ERROR  0x01
#define FW_CFG_DMA_CTL_READ   0x02
#define FW_CFG_DMA_CTL_SKIP   0x04
#define FW_CFG_DMA_CTL_SELECT 0x08
#define FW_CFG_DMA_CTL_WRITE  0x10

#define FW_CFG_FILE_DIR 0x19

struct QEMU_PACKED FWCfgFile {
    uint32_t size;
    uint16_t select;
    uint16_t reserved;
    char name[56];
};

struct QEMU_PACKED FWCfgDmaAccess {
    uint32_t control;
    uint32_t length;
    uint64_t address;
};

static void fw_cfg_dma_transfer(void* address,
                                uint32_t length,
                                uint32_t control) {
    /* QEMU DMA 看 PA：buffer 與 descriptor 都要 virt_to_phys。
     * access 必須 volatile，否則 -O2 會把 control 欄位讀進 reg 後 cache 住 */
    volatile struct FWCfgDmaAccess access;
    access.control = bswap32(control);
    access.length  = bswap32(length);
    access.address = bswap64(virt_to_phys((uint64_t)address));
    *FW_CFG_DMA = bswap64(virt_to_phys((uint64_t)&access));
    while (bswap32(access.control) & ~FW_CFG_DMA_CTL_ERROR)
        ;
}

static void fw_cfg_read_entry(void* buf, int e, int len) {
    uint32_t control = (e << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_READ;
    fw_cfg_dma_transfer(buf, len, control);
}

static int fw_cfg_find_file(const char* name) {
    uint32_t count = 0;
    fw_cfg_read_entry(&count, FW_CFG_FILE_DIR, sizeof(count));
    count = bswap32(count);
    for (uint32_t i = 0; i < count; i++) {
        struct FWCfgFile file;
        fw_cfg_dma_transfer(&file, sizeof(file), FW_CFG_DMA_CTL_READ);
        if (strncmp(name, file.name, sizeof(file.name)) == 0)
            return bswap16(file.select);
    }
    return -1;
}

#define CACHE_BLOCK_SIZE 64

// QEMU 路徑：沒模擬 D-cache，memory barrier 就夠。
// OrangePi 路徑：D-cache 實際存在，display controller 直接從 DRAM 讀，不 flush
// 會殘影／花屏。toolchain 沒 zicbom extension（gas 不認 cbo.flush mnemonic），
// 用 .word 0x0025200F 直寫 cbo.flush 0(a0) 的 raw encoding 繞過。
static void flush_dcache(void* addr, unsigned long len) {
#ifdef QEMU
    (void)addr;
    (void)len;
    __sync_synchronize();
#else
    unsigned long start = (unsigned long)addr & ~(CACHE_BLOCK_SIZE - 1);
    __sync_synchronize();
    for (unsigned long line = start;
         line < (unsigned long)addr + len;
         line += CACHE_BLOCK_SIZE) {
        unsigned long v = line;
        __asm__ __volatile__("mv a0, %0\n\t"
                             ".word 0x0025200F"      /* cbo.flush 0(a0) */
                             :: "r"(v)
                             : "memory", "a0");
        __sync_synchronize();
    }
#endif
}

void video_init(void) {
#ifdef QEMU
    struct RAMFBCfg cfg = {
        .addr   = bswap64(FB_BASE_PA),    /* QEMU 看 PA */
        .fourcc = bswap32(XRGB8888),
        .flags  = bswap32(0),
        .width  = bswap32(FB_WIDTH),
        .height = bswap32(FB_HEIGHT),
        .stride = bswap32(FB_WIDTH * FB_BPP),
    };
    int sel = fw_cfg_find_file("etc/ramfb");
    if (sel < 0) return;   // QEMU 沒帶 -device ramfb 就無聲跳過，kernel 還能跑
    uint32_t control = (sel << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE;
    fw_cfg_dma_transfer(&cfg, sizeof(cfg), control);
#else
    /* OrangePi RV2：U-Boot 開機已配置好 framebuffer，這裡無事可做 */
#endif
}

// 把 bmp 置中貼進 framebuffer。bmp 格式假設跟 FB 同樣 XRGB8888。
void video_bmp_display(unsigned int* bmp_image, int width, int height) {
    unsigned int* fb = (unsigned int*)FB_BASE_VA;   /* kernel 透過 high-VA linear map 寫 */
    int start_x = (FB_WIDTH - width) / 2;
    int start_y = (FB_HEIGHT - height) / 2;
    for (int y = 0; y < height; y++) {
        void* dst = fb + (start_y + y) * FB_WIDTH + start_x;
        memcpy(dst, bmp_image + y * width, width * sizeof(unsigned int));
        flush_dcache(dst, width * sizeof(unsigned int));
    }
}
