#ifndef UART_H
#define UART_H

// 預設 UART base address（開機時用，DTB 解析前）
#ifdef QEMU
// #define UART_BASE_DEFAULT 0x10000000UL
#define UART_LSR_OFFSET   0x5
#else
// #define UART_BASE_DEFAULT 0xD4017000UL
#define UART_LSR_OFFSET   0x14
#endif

// 動態 UART base address（DTB 解析後會更新）
extern unsigned long uart_base;

#define UART_RBR  ((volatile unsigned char*)(uart_base + 0x0))
#define UART_THR  ((volatile unsigned char*)(uart_base + 0x0))
#define UART_LSR  ((volatile unsigned char*)(uart_base + UART_LSR_OFFSET))

#define LSR_DR    (1 << 0)  // data ready
#define LSR_TDRQ  (1 << 5)  // transmitter holding register empty

void uart_putc(char c);
char uart_getc();
unsigned char uart_getb();
void uart_puts(const char* s);
void uart_hex(unsigned long h);
void uart_int(unsigned long n);
void uart_init(const void* fdt);

#endif
