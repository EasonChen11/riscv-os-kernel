/* user-side mini libc。沒 libc／libgcc 可連，所有 helper 自己 inline 實作。
 * 對 freestanding：byte-loop 仍可能被 GCC lower 成 memcpy/memset call，目前
 * 沒用到 memcpy／memset 所以不爆；之後加複雜 helper 注意自己 provide symbol。
 */
#include "lib.h"

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
