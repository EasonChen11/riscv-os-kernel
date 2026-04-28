/* user-side mini libc。沒 libc／libgcc 可連，所有 helper 自己 inline 實作。
 * 對 freestanding：byte-loop 仍可能被 GCC lower 成 memcpy/memset call，目前
 * 沒用到 memcpy／memset 所以不爆；之後加複雜 helper 注意自己 provide symbol。
 */
#include "lib.h"
#include <stdarg.h>

void puts_(const char* s) {
    long n = 0;
    while (s[n]) n++;
    uart_write(s, n);
}

void putc_(char c) {
    uart_write(&c, 1);
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

int strncmp(const char* a, const char* b, int n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return *(unsigned char*)a - *(unsigned char*)b;
}

int atoi(const char* s) {
    int r = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        r = r * 10 + (*s - '0');
        s++;
    }
    return neg ? -r : r;
}

/* 簡單 decimal print。負數先印 '-' 再走絕對值；0 特判直接印 '0'。 */
void print_int(long n) {
    char buf[24];
    int  i = 0;
    if (n < 0) { putc_('-'); n = -n; }
    if (n == 0) { putc_('0'); return; }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) putc_(buf[--i]);
}

/* 16 進位 print，無 0x prefix。 */
static void print_hex(unsigned long n) {
    char buf[16];
    int  i = 0;
    if (n == 0) { putc_('0'); return; }
    while (n > 0) {
        unsigned d = n & 0xF;
        buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
        n >>= 4;
    }
    while (i > 0) putc_(buf[--i]);
}

/* 極簡 printf。RISC-V LP64 ABI：variadic int 也會以 64-bit slot 傳遞且
 * sign-extend，所以 %d / %ld 一律 va_arg(ap, long) 都正確。 */
void printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') { putc_(*p); continue; }
        p++;
        if (*p == 'l') p++;     // 吃掉 'l' 修飾，因為一律當 long 處理
        switch (*p) {
            case 'd':
                print_int(va_arg(ap, long));
                break;
            case 'x':
                print_hex(va_arg(ap, unsigned long));
                break;
            case 'p':
                puts_("0x");
                print_hex(va_arg(ap, unsigned long));
                break;
            case 's':
                puts_(va_arg(ap, const char*));
                break;
            case 'c':
                putc_((char)va_arg(ap, int));
                break;
            case '%':
                putc_('%');
                break;
            case 0:                  // fmt 以 '%' 結尾，原樣印出 % 並退出
                putc_('%');
                va_end(ap);
                return;
            default:                 // 不支援的 specifier，原樣印 % + 字
                putc_('%');
                putc_(*p);
                break;
        }
    }
    va_end(ap);
}

/* readline：echo 字符 + 處理 backspace；遇 \n 換行收工。
 * 沒處理 \r→\n 因為 kernel-side uart_try_getc 已轉換過了。 */
int readline(char* buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c;
        uart_read(&c, 1);
        if (c == '\n') {
            putc_('\n');
            buf[i] = 0;
            return i;
        }
        if (c == '\b' || c == 127) {
            if (i > 0) {
                i--;
                putc_('\b'); putc_(' '); putc_('\b');
            }
        } else {
            buf[i++] = c;
            putc_(c);
        }
    }
    buf[i] = 0;
    return i;
}
