#ifndef USER_LIB_H
#define USER_LIB_H

/* syscall wrappers — impl in syscall.S */
long getpid(void);
long uart_read(char* buf, long count);
long uart_write(const char* buf, long count);
int  exec(const char* path);
long fork(void);
long waitpid(long pid);
void exit(int status) __attribute__((noreturn));
int  stop(long pid);
void display(unsigned int* bmp, unsigned int w, unsigned int h);
int  usleep(unsigned int usec);
long signal(int signum, void (*handler)(void));   // 註冊 user-mode handler；handler==0 表示恢復 default
long kill(int pid, int signum);                   // 發 signal 給目標 pid

/* mini libc — impl in lib.c */
void puts(const char* s);              /* 印字串 + '\n'，行為對齊 stdlib */
void putchar(char c);
int  strcmp(const char* a, const char* b);
int  strncmp(const char* a, const char* b, int n);
int  atoi(const char* s);
void print_int(long n);
int  readline(char* buf, int max);     /* 讀到 \n 為止，回 byte 數（不含 null） */

/* 極簡 printf：支援 %d (long-wide) %x %p %s %c %%。RISC-V LP64 下 va_arg(long)
 * 對 int / long 都正確（int 會被 sign-extend 成 64-bit slot 傳遞）。 */
void printf(const char* fmt, ...);

#endif /* USER_LIB_H */
