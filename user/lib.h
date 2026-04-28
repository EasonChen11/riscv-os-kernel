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

/* mini libc — impl in lib.c */
void puts_(const char* s);             /* 不送 \n（避免跟 stdlib puts 簽名衝突） */
void putc_(char c);
int  strcmp(const char* a, const char* b);
int  strncmp(const char* a, const char* b, int n);
int  atoi(const char* s);
void print_int(long n);
int  readline(char* buf, int max);     /* 讀到 \n 為止，回 byte 數（不含 null） */

#endif /* USER_LIB_H */
