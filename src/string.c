#include "string.h"

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, unsigned long n) {
    while (n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2; // s1 > s2: positive, s1 < s2: negative
}

unsigned long strlen(const char* s) {
    unsigned long len = 0;
    while (*s++) len++;
    return len;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (char*)0;
}

// freestanding 環境沒 libc memcpy；GCC 對 byte-loop 會 lower 成 memcpy call
// （loop idiom recognition），所以這個 symbol 必須自己提供，不然 link 失敗。
void* memcpy(void* dst, const void* src, unsigned long n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memset(void* dst, int c, unsigned long n) {
    unsigned char* d = (unsigned char*)dst;
    unsigned char v = (unsigned char)c;
    while (n--) *d++ = v;
    return dst;
}
