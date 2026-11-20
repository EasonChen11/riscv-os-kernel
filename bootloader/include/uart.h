#ifndef UART_H
#define UART_H

extern unsigned long uart_base;
extern unsigned long uart_shift;

#define UART_REG(r) ((volatile unsigned char *)(uart_base + ((r) << uart_shift)))
#define UART_RBR    UART_REG(0)
#define UART_THR    UART_REG(0)
#define UART_IER    UART_REG(1)
#define UART_FCR    UART_REG(2)
#define UART_LCR    UART_REG(3)
#define UART_LSR    UART_REG(5)

#define LSR_DR      (1 << 0)   /* data ready */
#define LSR_TDRQ    (1 << 5)   /* THR empty */

void uart_init(const void *fdt);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(unsigned long h);
unsigned char uart_getb(void);

#endif
