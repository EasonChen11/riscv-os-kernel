#ifndef STRING_H
#define STRING_H

#include <stdint.h>


int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, unsigned long n);
unsigned long strlen(const char* s);
char* strchr(const char* s, int c);
void* memcpy(void* dst, const void* src, unsigned long n);
void* memset(void* dst, int c, unsigned long n);

#endif
