#include "uart.h"

void bootloader_main(unsigned long hartid, const void *dtb)
{
    (void)hartid;
    uart_init(dtb);
    uart_puts("bootloader: hello from QEMU\n");
    for (;;)
        asm volatile("wfi");
}
