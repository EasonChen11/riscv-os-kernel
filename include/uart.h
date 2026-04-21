#ifndef UART_H
#define UART_H

// 預設 UART base address（開機時用，DTB 解析前）
// #ifdef QEMU
// #define UART_BASE_DEFAULT 0x10000000UL
// #define UART_LSR_OFFSET   0x5
// #else
// #define UART_BASE_DEFAULT 0xD4017000UL
// #define UART_LSR_OFFSET   0x14
// #endif

// 動態 UART base address（DTB 解析後會更新）
extern unsigned long uart_base;
extern unsigned long uart_shift; // 0 for 16550A(QEMU), 2 for SiFive UART (4 bytes per register)

enum UART_REG {
    RBR = 0,  // Receiver Buffer Register (read-only)
    THR = 0, // Transmitter Holding Register (write-only)
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

void uart_putc(char c);
char uart_getc();
unsigned char uart_getb();
void uart_puts(const char* s);
void uart_hex(unsigned long h);
void uart_int(unsigned long n);
void uart_init(const void* fdt);

#endif
