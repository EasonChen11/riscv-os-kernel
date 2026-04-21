#ifndef UART_H
#define UART_H

#include <stdbool.h>

#define MODE 1 // 0 for polling mode, 1 for interrupt mode

// 動態 UART base / reg-shift（uart_init 從 DTB 填入）
extern unsigned long uart_base;
extern unsigned long uart_shift;   // 0 for 16550A(QEMU), 2 for DW/SiFive UART (4-byte stride)

enum UART_REG {
    RBR = 0,  // Receiver Buffer Register (read-only)
    THR = 0,  // Transmitter Holding Register (write-only)
    IER = 1,  // Interrupt Enable Register
    IIR = 2,  // Interrupt Identification Register (read-only)
    FCR = 2,  // FIFO Control Register (write-only)
    LCR = 3,  // Line Control Register
    MCR = 4,  // Modem Control Register
    LSR = 5,  // Line Status Register
};

#define UART_RBR  ((volatile unsigned char*)(uart_base + (RBR << uart_shift)))
#define UART_THR  ((volatile unsigned char*)(uart_base + (THR << uart_shift)))
#define UART_IER  ((volatile unsigned char*)(uart_base + (IER << uart_shift)))
#define UART_IIR  ((volatile unsigned char*)(uart_base + (IIR << uart_shift)))
#define UART_FCR  ((volatile unsigned char*)(uart_base + (FCR << uart_shift)))
#define UART_LCR  ((volatile unsigned char*)(uart_base + (LCR << uart_shift)))
#define UART_MCR  ((volatile unsigned char*)(uart_base + (MCR << uart_shift)))
#define UART_LSR  ((volatile unsigned char*)(uart_base + (LSR << uart_shift)))

#define LSR_DR    (1 << 0)  // data ready
#define LSR_TDRQ  (1 << 5)  // transmitter holding register empty

// 初始化 / 輸出（兩種模式共用）
void uart_init(const void* fdt);
void uart_putc(char c);
void uart_puts(const char* s);
void uart_hex(unsigned long h);
void uart_int(unsigned long n);

// 輸入（polling / interrupt 由 MODE 切換實作）
char          uart_getc(void);             // 阻塞讀一字
bool          uart_try_getc(char *out);    // 非阻塞；無字回 false
unsigned char uart_getb(void);             // 讀 raw byte，不做 \r→\n；bootloader 用

// Interrupt 專用。polling 模式下定義為 no-op，main/trap 可無條件呼叫
void uart_irq_init(void);
void uart_handle_irq(unsigned int irq);

#endif
