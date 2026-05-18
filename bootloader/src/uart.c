#include "uart.h"
#include "fdt.h"

unsigned long uart_base  = 0x10000000UL;   /* fallback：QEMU virt 16550 */
unsigned long uart_shift = 0;

void uart_init(const void *fdt)
{
    /* QEMU virt: /soc/uart；OrangePi RV2: /soc/serial */
    int off = fdt_path_offset(fdt, "/soc/serial");
    if (off < 0)
        off = fdt_path_offset(fdt, "/soc/uart");
    if (off < 0)
        return;

    int soc = fdt_path_offset(fdt, "/soc");
    unsigned long addr = 0, size = 0;
    if (fdt_get_reg(fdt, soc, off, 0, &addr, &size) < 0)
        return;
    if (addr != 0)
        uart_base = addr;   /* PA 直接用，無 MMU */

    int plen = 0;
    const void *prop = fdt_getprop(fdt, off, "reg-shift", &plen);
    if (prop)
        uart_shift = fdt_read_cells((const uint32_t *)prop, 1);
}

void uart_putc(char c)
{
    if (c == '\n')
        uart_putc('\r');
    while ((*UART_LSR & LSR_TDRQ) == 0)
        ;
    *UART_THR = c;
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

void uart_hex(unsigned long h)
{
    uart_puts("0x");
    for (int c = 60; c >= 0; c -= 4) {
        unsigned long n = (h >> c) & 0xf;
        uart_putc(n > 9 ? (char)(n + 0x57) : (char)(n + '0'));
    }
}

unsigned char uart_getb(void)
{
    while ((*UART_LSR & LSR_DR) == 0)
        ;
    return *UART_RBR;
}
