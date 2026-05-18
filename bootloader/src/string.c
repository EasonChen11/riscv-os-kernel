#include "string.h"

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, unsigned long n)
{
    while (n && *s1 && *s1 == *s2) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

unsigned long strlen(const char *s)
{
    unsigned long n = 0;
    while (*s++) n++;
    return n;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : (char *)0;
}
