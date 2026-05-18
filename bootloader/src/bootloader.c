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

void bootloader_main(unsigned long hartid, const void *dtb)
{
    (void)hartid;

    bootinfo_init(dtb);
    uart_init(dtb);

    uart_puts("bootloader: 等待資訊...\n");
    uart_puts("  mem_base = "); uart_hex(g_mem_base); uart_puts("\n");
    uart_puts("  mem_size = "); uart_hex(g_mem_size); uart_puts("\n");
    uart_puts("  reloc_pc = "); uart_hex((unsigned long)&bootloader_main); uart_puts("\n");
    uart_puts("  _end     = "); uart_hex((unsigned long)_end); uart_puts("\n");

    for (;;)
        asm volatile("wfi");
}
