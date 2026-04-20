#include "bootloader.h"
#include "uart.h"
#include "fdt.h"
#include "bootinfo.h"

// ─────────────────────────────────────────────────────────────
// Self-relocation policy
// ─────────────────────────────────────────────────────────────
// 解析 DTB /memory 的 reg 屬性，從記憶體頂端往下找一個 2 MiB 對齊
// 且不會覆蓋 DTB 與 initrd 的位址作為 kernel 自我搬遷目標。
// kernel_size 是 _start ~ _end 之間的位元組數。
// ⚠️ 在 BSS 清零前被呼叫，只能使用區域變數，不可依賴全域/靜態變數。
unsigned long bootloader_find_reloc_addr(const void* fdt, unsigned long kernel_size) {
    int root = fdt_root_offset(fdt);
    if (root < 0)
        return 0;

    int mem = fdt_path_offset(fdt, "/memory");
    if (mem < 0)
        return 0;

    unsigned long mem_base, mem_size;
    if (fdt_get_reg(fdt, root, mem, 0, &mem_base, &mem_size) < 0)
        return 0;

    // 取得 DTB 範圍
    unsigned long dtb_start = (unsigned long)fdt;
    unsigned long dtb_end = dtb_start + fdt_total_size(fdt);

    // 取得 initrd 範圍。linux,initrd-start/end 為純量，cell 數依 property 長度判斷。
    unsigned long initrd_start = 0, initrd_end = 0;
    int chosen_off = fdt_path_offset(fdt, "/chosen");
    if (chosen_off >= 0) {
        int plen = 0;
        const void* prop = fdt_getprop(fdt, chosen_off, "linux,initrd-start", &plen);
        if (prop) {
            if (plen == 8)      initrd_start = fdt_read_cells(prop, 2);
            else if (plen == 4) initrd_start = fdt_read_cells(prop, 1);
        }
        prop = fdt_getprop(fdt, chosen_off, "linux,initrd-end", &plen);
        if (prop) {
            if (plen == 8)      initrd_end = fdt_read_cells(prop, 2);
            else if (plen == 4) initrd_end = fdt_read_cells(prop, 1);
        }
    }
    // 從記憶體頂端向下找一個 2 MiB 對齊、不會覆蓋 DTB / initrd / /reserved-memory 的位置。
    // 因為呼叫時 BSS 尚未清零，全程只用 local 變數；fdt_* walker 皆 stateless，可安全使用。
    unsigned long mem_end = mem_base + mem_size;
    unsigned long reloc_addr = (mem_end - kernel_size) & ~(0x200000UL - 1); // 對齊到 2 MiB（0x200000），確保符合 RISC-V SBI 的載入要求

    // 最多迭代 MAX_RETRY 次，避免惡意 fdt 造成無窮迴圈
    for (int iter = 0; iter < 64; iter++) {
        int conflict = 0;

        // DTB
        if (reloc_addr < dtb_end && (reloc_addr + kernel_size) > dtb_start) {
            reloc_addr = (dtb_start - kernel_size) & ~(0x200000UL - 1);
            conflict = 1;
            continue;
        }

        // initrd
        if (initrd_end > 0 && reloc_addr < initrd_end && (reloc_addr + kernel_size) > initrd_start) {
            reloc_addr = (initrd_start - kernel_size) & ~(0x200000UL - 1);
            conflict = 1;
            continue;
        }

        // /reserved-memory 子節點
        int rm = fdt_path_offset(fdt, "/reserved-memory");
        if (rm >= 0) {
            for (int sub = fdt_first_subnode(fdt, rm); sub >= 0; sub = fdt_next_subnode(fdt, sub)) {
                unsigned long base, size;
                if (fdt_get_reg(fdt, rm, sub, 0, &base, &size) < 0 || size == 0)
                    continue;
                unsigned long end = base + size;
                if (reloc_addr < end && (reloc_addr + kernel_size) > base) {
                    reloc_addr = (base - kernel_size) & ~(0x200000UL - 1);
                    conflict = 1;
                    break;
                }
            }
        }
        if (conflict) continue;

        // 無任何衝突
        break;
    }

    if (reloc_addr < mem_base)
        return 0;

    return reloc_addr;
}

// ─────────────────────────────────────────────────────────────
// Load a fresh kernel image via UART and jump to it
// ─────────────────────────────────────────────────────────────
// 經由 UART 接收新的 kernel image，驗證 magic，寫入固定載入位址
// (KERNEL_ADDR)，最後跳轉執行。
typedef void (*kernel_func_t)(unsigned long, const void *);

#ifdef QEMU
#define KERNEL_ADDR 0x80200000UL
#else
#define KERNEL_ADDR 0x00200000UL
#endif

#define BOOT_MAGIC 0x544F4F42  // "BOOT" in little-endian

void bootloader_load_kernel(void) {
    uart_puts("Waiting for kernel image via UART...\n");

    // check the load memory is correct
    if (KERNEL_ADDR < g_mem_base || KERNEL_ADDR >= g_mem_base + g_mem_size) {
        uart_puts("Error: kernel load address is out of memory range.\n");
        return;
    }else {
        uart_puts("Kernel will be loaded to ");
        uart_hex(KERNEL_ADDR);
        uart_puts(".\n");
    }
    // 從 UART 讀取 8 byte header（protocol: magic 4B + size 4B）
    unsigned char header[8];
    for (int i = 0; i < 8; i++)
        header[i] = uart_getb();  // uart_getb: 讀取原始 byte，不做 \r→\n 轉換

    // 將 header 前 4 bytes 組合成 little-endian uint32 作為 magic number
    // header[0] 是最低位元組，header[3] 是最高位元組
    unsigned int magic = header[0] | (header[1] << 8) |
                         (header[2] << 16) | (header[3] << 24);
    // 將 header 後 4 bytes 組合成 little-endian uint32 作為 kernel 大小
    unsigned int size  = header[4] | (header[5] << 8) |
                         (header[6] << 16) | (header[7] << 24);

    // 驗證 magic number 是否為 "BOOT"（0x544F4F42）
    // 防止非預期資料被當作 kernel 寫入記憶體
    if (magic != BOOT_MAGIC) {
        uart_puts("Error: invalid magic number.\n");
        return;
    }

    uart_puts("Receiving kernel (");
    uart_hex(size);
    uart_puts(" bytes) to ");
    uart_hex(KERNEL_ADDR);
    uart_puts(" ...\n");

    // 將 kernel binary 逐 byte 從 UART 讀入目標位址
    // KERNEL_ADDR = 0x80200000 (QEMU) / 0x00200000 (Board)
    // 因為 bootloader 已自我搬遷，此位址現在是空的，可以安全寫入
    unsigned char *dst = (unsigned char *)KERNEL_ADDR;
    for (unsigned int i = 0; i < size; i++)
        dst[i] = uart_getb();

    uart_puts("Kernel loaded. Jumping to kernel...\n");

    // 清除 I-Cache，確保 CPU 取指令時讀到剛寫入的新 kernel
    // asm volatile(".4byte 0x0000100f");
    asm volatile("fence.i");

    kernel_func_t kernel_entry = (kernel_func_t)KERNEL_ADDR; // kernel entry point is the start of the loaded image
    kernel_entry(0, g_fdt_base); // c call function like jump to kernel, pass fdt_base as argument for kernel to read DTB if needed
}
