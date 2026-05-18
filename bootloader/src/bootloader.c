#include "uart.h"
#include "bootinfo.h"
#include "fdt.h"

/* ──────────────────────────────────────────────────────────────
 * Self-relocation policy
 * ──────────────────────────────────────────────────────────────
 * 解析 DTB /memory 的 reg 屬性，從記憶體頂端往下找 2 MiB 對齊、
 * 不覆蓋 DTB / initrd / /reserved-memory 的位址。
 * ⚠️ start.S 在 BSS 清零前呼叫，只能用區域變數。
 */
unsigned long bootloader_find_reloc_addr(const void *fdt, unsigned long kernel_size)
{
    int root = fdt_root_offset(fdt);
    if (root < 0) return 0;

    int mem = fdt_path_offset(fdt, "/memory");
    if (mem < 0) return 0;

    unsigned long mem_base, mem_size;
    if (fdt_get_reg(fdt, root, mem, 0, &mem_base, &mem_size) < 0)
        return 0;

    unsigned long dtb_start = (unsigned long)fdt;
    unsigned long dtb_end   = dtb_start + fdt_total_size(fdt);

    unsigned long initrd_start = 0, initrd_end = 0;
    int chosen_off = fdt_path_offset(fdt, "/chosen");
    if (chosen_off >= 0) {
        int plen = 0;
        const void *prop = fdt_getprop(fdt, chosen_off, "linux,initrd-start", &plen);
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

    unsigned long mem_end = mem_base + mem_size;
    unsigned long reloc_addr = (mem_end - kernel_size) & ~(0x200000UL - 1);

    for (int iter = 0; iter < 64; iter++) {
        int conflict = 0;

        if (reloc_addr < dtb_end && (reloc_addr + kernel_size) > dtb_start) {
            reloc_addr = (dtb_start - kernel_size) & ~(0x200000UL - 1);
            conflict = 1;
            continue;
        }

        if (initrd_end > 0 && reloc_addr < initrd_end && (reloc_addr + kernel_size) > initrd_start) {
            reloc_addr = (initrd_start - kernel_size) & ~(0x200000UL - 1);
            conflict = 1;
            continue;
        }

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
        break;
    }

    if (reloc_addr < mem_base)
        return 0;

    return reloc_addr;
}

extern char _end[];

#ifdef QEMU
#define KERNEL_ADDR 0x80200000UL
#else
#define KERNEL_ADDR 0x00200000UL
#endif

#define BOOT_MAGIC  0x544F4F42UL    /* "BOOT" little-endian */

typedef void (*kernel_func_t)(unsigned long, const void *);

static void halt(void)
{
    for (;;)
        asm volatile("wfi");
}

/* UART 收 8-byte header (magic + size, LE)，讀 size bytes 到 KERNEL_ADDR，跳轉 */
void bootloader_load_kernel(unsigned long hartid, const void *dtb)
{
    if (KERNEL_ADDR < g_mem_base || KERNEL_ADDR >= g_mem_base + g_mem_size) {
        uart_puts("Error: KERNEL_ADDR out of memory range\n");
        halt();
    }

    uart_puts("Kernel will be loaded to "); uart_hex(KERNEL_ADDR); uart_puts("\n");

    unsigned char hdr[8];
    for (int i = 0; i < 8; i++)
        hdr[i] = uart_getb();

    unsigned int magic = (unsigned int)hdr[0]
                       | ((unsigned int)hdr[1] << 8)
                       | ((unsigned int)hdr[2] << 16)
                       | ((unsigned int)hdr[3] << 24);
    unsigned int size  = (unsigned int)hdr[4]
                       | ((unsigned int)hdr[5] << 8)
                       | ((unsigned int)hdr[6] << 16)
                       | ((unsigned int)hdr[7] << 24);

    if (magic != BOOT_MAGIC) {
        uart_puts("Error: invalid magic ("); uart_hex(magic); uart_puts(")\n");
        halt();
    }

    uart_puts("Receiving "); uart_hex(size); uart_puts(" bytes...\n");

    unsigned char *dst = (unsigned char *)KERNEL_ADDR;
    for (unsigned int i = 0; i < size; i++)
        dst[i] = uart_getb();

    uart_puts("Kernel loaded. Jumping...\n");

    asm volatile("fence.i");

    kernel_func_t entry = (kernel_func_t)KERNEL_ADDR;
    entry(hartid, dtb);

    halt();   /* 不會回來，保險 */
}

void bootloader_main(unsigned long hartid, const void *dtb)
{
    bootinfo_init(dtb);
    uart_init(dtb);

    uart_puts("bootloader: 等待資訊...\n");
    uart_puts("  mem_base = "); uart_hex(g_mem_base); uart_puts("\n");
    uart_puts("  mem_size = "); uart_hex(g_mem_size); uart_puts("\n");
    uart_puts("  reloc_pc = "); uart_hex((unsigned long)&bootloader_main); uart_puts("\n");

    bootloader_load_kernel(hartid, dtb);
}
