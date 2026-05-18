#include "uart.h"
#include "bootinfo.h"

void bootloader_main(unsigned long hartid, const void *dtb)
{
    (void)hartid;

    bootinfo_init(dtb);
    uart_init(dtb);

    uart_puts("bootloader: 等待資訊...\n");
    uart_puts("  mem_base = "); uart_hex(g_mem_base); uart_puts("\n");
    uart_puts("  mem_size = "); uart_hex(g_mem_size); uart_puts("\n");

    for (;;)
        asm volatile("wfi");
}
