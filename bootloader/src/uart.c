#include "uart.h"

unsigned long uart_base  = 0x10000000UL;   /* QEMU virt 16550，Task 5 改成 DTB-driven */
unsigned long uart_shift = 0;

void uart_init(const void *fdt)
{
    (void)fdt;
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
