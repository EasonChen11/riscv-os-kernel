#include "uart.h"
#include "fdt.h"
#include "string.h"

// 開機預設值，DTB 解析前就能用
unsigned long uart_base = 0x0L;

void uart_init(const void* fdt) {
    // 從 DTB 讀取 UART base address
    // QEMU: /soc/uart, OrangePi RV2: /soc/serial
#ifdef QEMU
    int offset = fdt_path_offset(fdt, "/soc/uart");
    if (offset < 0) { // QEMU 的 DTB 可能用 /soc/serial 作為 UART 節點名稱
        offset = fdt_path_offset(fdt, "/soc/serial");
    }
#else
    int offset = fdt_path_offset(fdt, "/soc/serial");
#endif
    if (offset < 0) {
        uart_puts("uart_init: node not found\n");
        return;
    }

    // UART 的 reg 由其 parent (/soc) 的 #address-cells / #size-cells 決定。
    int soc = fdt_path_offset(fdt, "/soc");
    unsigned long addr = 0, size = 0;
    if (fdt_get_reg(fdt, soc, offset, 0, &addr, &size) < 0) {
        uart_puts("uart_init: reg parse failed\n");
        return;
    }

    if (addr != 0)
        uart_base = addr;
}

char uart_getc() {
    while ((*UART_LSR & LSR_DR) == 0)
        ;
    char c = (char)*UART_RBR;
    return c == '\r' ? '\n' : c;
}

unsigned char uart_getb() {
    while ((*UART_LSR & LSR_DR) == 0)
        ;
    return *UART_RBR;
}

void uart_putc(char c) {
    if (c == '\n')
        uart_putc('\r');

    while ((*UART_LSR & LSR_TDRQ) == 0)
        ;
    *UART_THR = c;
}

void uart_puts(const char* s) {
    while (*s)
        uart_putc(*s++);
}

void uart_hex(unsigned long h) {
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0'; // 0x57 = 87 = 'a' - 10, 0-9 → '0'-'9', 10-15 → 'a'-'f'
        uart_putc(n);
    }
}

void uart_int(unsigned long n) {
    if (n == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--)
        uart_putc(buf[j]);
}