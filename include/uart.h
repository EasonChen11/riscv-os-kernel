#ifndef UART_H
#define UART_H

#include <stdbool.h>

#define MODE 1 // 0 for polling mode, 1 for interrupt mode

// 動態 UART base / reg-shift（uart_init 從 DTB 填入）
extern unsigned long uart_base;
extern unsigned long uart_shift;   // 0 for 16550A(QEMU), 2 for DW/SiFive UART (4-byte stride)

enum UART_REG {
    RBR = 0,  // Receiver Buffer Register (read-only), spec: 16.3.4.1
    THR = 0,  // Transmitter Holding Register (write-only), spec: 16.3.4.2
    IER = 1,  // Interrupt Enable Register, spec: 16.3.4.5
    IIR = 2,  // Interrupt Identification Register (read-only), spec: 16.3.4.6
    FCR = 2,  // FIFO Control Register (write-only), spec: 16.3.4.7
    LCR = 3,  // Line Control Register, spec: 16.3.4.8
    MCR = 4,  // Modem Control Register, spec : 16.3.4.9
    LSR = 5,  // Line Status Register, spec: 16.3.4.10
};

#define UART_RBR  ((volatile unsigned char*)(uart_base + (RBR << uart_shift)))
#define UART_THR  ((volatile unsigned char*)(uart_base + (THR << uart_shift)))
#define UART_IER  ((volatile unsigned char*)(uart_base + (IER << uart_shift)))
#define UART_IIR  ((volatile unsigned char*)(uart_base + (IIR << uart_shift)))
#define UART_FCR  ((volatile unsigned char*)(uart_base + (FCR << uart_shift)))
#define UART_LCR  ((volatile unsigned char*)(uart_base + (LCR << uart_shift)))
#define UART_MCR  ((volatile unsigned char*)(uart_base + (MCR << uart_shift)))
#define UART_LSR  ((volatile unsigned char*)(uart_base + (LSR << uart_shift)))

#define IER_RAVIE (1 << 0) // Receiver data Available Interrupt Enable    -> RBR 有資料時觸發 IRQ
#define IER_TIE   (1 << 1) // Transmit data request Interrupt Enable      -> THR 空時觸發 IRQ
#define IER_UUE   (1 << 6) // UART Unit Enable (SoC-specific，K1 專用)    -> 整個 UART 啟用；u-boot 遺留為 1，不可清

// LCR bits
#define LCR_DLAB  (1 << 7) // Divisor Latch Access Bit                   -> 切換 offset 0/1 為 DLL/DLH (baud divisor) 或 RBR/IER

// FCR bits (FIFO Control Register，write-only)
#define FCR_TRFIFOE (1 << 0) // Transmit & Receive FIFO Enable             -> 啟用 16-byte FIFO
#define FCR_RESETRF (1 << 1) // Reset Receive FIFO                         -> 清空 RX FIFO
#define FCR_RESETTF (1 << 2) // Reset Transmit FIFO                        -> 清空 TX FIFO
// FCR bit 7:6 — Interrupt Trigger Level (RX FIFO 累積到幾 byte 才觸發 RX IRQ)
#define FCR_ITL_1BYTE   (0 << 6) // ITL: 1 byte                           -> 互動最即時（shell 適用）
#define FCR_ITL_4BYTE   (1 << 6) // ITL: 4 bytes                          -> 中等延遲、IRQ 較少
#define FCR_ITL_8BYTE   (2 << 6) // ITL: 8 bytes                          -> 偏高吞吐量
#define FCR_ITL_14BYTE  (3 << 6) // ITL: 14 bytes                         -> 最高吞吐量、人機互動會卡

// MCR bits
#define MCR_OUT2  (1 << 3) // Output 2                                   -> UART IRQ 輸出連到 PLIC

// LSR bits (Line Status Register)
#define LSR_DR    (1 << 0) // Data Ready                                 -> RX 有資料可讀
#define LSR_TDRQ  (1 << 5) // Transmit Data ReQuest                       -> THR 可寫入（等同標準 16550 THRE；shift register 可能仍在送出）

// 初始化 / 輸出（兩種模式共用）
void uart_init(const void* fdt);
void uart_init_print_info(void);   // interrupt 模式下須延後到 uart_irq_init 之後才能呼叫
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
