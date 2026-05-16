#include "bootinfo.h"
#include "fdt.h"
#include "uart.h"
#include "sbi.h"
#include "pgtable.h"

// linker script 提供的 kernel 範圍符號
extern char _start[];
extern char _end[];

const void*   g_fdt_base          = (void*)0;
unsigned long g_fdt_size          = 0;
const void*   g_initrd_base       = (void*)0;
unsigned long g_initrd_end        = 0;
unsigned long g_mem_base          = 0;
unsigned long g_mem_size          = 0;
unsigned long g_kernel_reloc_addr = 0;
unsigned long g_kernel_start      = 0;
unsigned long g_kernel_end        = 0;

void bootinfo_init(const void* fdt, unsigned long reloc_addr) {
    g_fdt_base          = fdt;
    g_kernel_reloc_addr = reloc_addr;
    g_kernel_start      = (unsigned long)_start;
    g_kernel_end        = (unsigned long)_end;

    // DTB 總大小
    g_fdt_size = fdt_total_size(fdt);

    int root = fdt_root_offset(fdt);

    // /chosen → initrd 範圍。linux,initrd-start/end 為純量，使用 root 的 #address-cells 編碼。
    int chosen = fdt_path_offset(fdt, "/chosen");
    if (chosen >= 0) {
        int len = 0;                                                                                                    
        const void* prop = fdt_getprop(fdt, chosen, "linux,initrd-start", &len);
        // 2 cases: 64-bit initrd base (len=8) or 32-bit initrd base (len=4)。實驗中以 32-bit 為主。
        if (prop) {
            unsigned long pa = (len == 8) ? fdt_read_cells(prop, 2) : fdt_read_cells(prop, 1);
            g_initrd_base = (const void*)phys_to_virt(pa);
        }
        prop = fdt_getprop(fdt, chosen, "linux,initrd-end", &len);
        if (prop) {
            unsigned long pa = (len == 8) ? fdt_read_cells(prop, 2) : fdt_read_cells(prop, 1);
            g_initrd_end = phys_to_virt(pa);
        }                                                 
    } else {
        uart_puts("bootinfo: /chosen not found\n");
    }

    // /memory → 第一段 reg。parent 為 root，cells 由 root 的 #address-cells/#size-cells 決定。
    int mem = fdt_path_offset(fdt, "/memory");
    if (mem >= 0) {
        if (fdt_get_reg(fdt, root, mem, 0, &g_mem_base, &g_mem_size) < 0)
            uart_puts("bootinfo: /memory reg parse failed\n");
    } else {
        uart_puts("bootinfo: /memory not found\n");
    }
}

void bootinfo_print(void) {
    uart_puts("System informaion: \n");
    uart_puts("  OpenSBI specification version: ");
    uart_hex(sbi_get_spec_version());
    uart_puts("\n  implementation ID: ");
    uart_hex(sbi_get_imp_id());
    uart_puts("\n  implementation version: ");
    uart_hex(sbi_get_imp_version());
    uart_puts("\n  uart base: ");
    uart_hex(uart_base);
    uart_puts("\n  fdt base: ");
    uart_hex((unsigned long)g_fdt_base);
    uart_puts("\n  fdt size: ");
    uart_hex(g_fdt_size);
    uart_puts("\n  initrd base: ");
    uart_hex((unsigned long)g_initrd_base);
    uart_puts("\n  initrd end: ");
    uart_hex(g_initrd_end);
    uart_puts("\n  mem base: ");
    uart_hex(g_mem_base);
    uart_puts("\n  mem size: ");
    uart_hex(g_mem_size);
    uart_puts("\n  kernel start: ");
    uart_hex(g_kernel_start);
    uart_puts("\n  kernel end: ");
    uart_hex(g_kernel_end);
    uart_puts("\n  kernel relocated to: ");
    uart_hex(g_kernel_reloc_addr);
    uart_puts("\n");
}
