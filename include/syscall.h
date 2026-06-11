#ifndef SYSCALL_H
#define SYSCALL_H

#include "trap.h"

// syscall 編號：user binary 與 kernel dispatcher 共用
enum {
    SYS_GETPID     = 0,
    SYS_UART_READ  = 1,
    SYS_UART_WRITE = 2,
    SYS_EXEC       = 3,
    SYS_FORK       = 4,
    SYS_WAITPID    = 5,
    SYS_EXIT       = 6,
    SYS_STOP       = 7,
    SYS_DISPLAY    = 8,
    SYS_USLEEP     = 9,
    SYS_SIGNAL     = 10,
    SYS_SIGRETURN  = 11,
    SYS_KILL       = 12,
    SYS_MMAP       = 13,
    SYS_OPEN       = 14,
    SYS_CLOSE      = 15,
    SYS_READ       = 16,
    SYS_WRITE      = 17,
    SYS_MKDIR      = 18,
    SYS_MOUNT      = 19,
    SYS_CHDIR      = 20,
    SYS_LSEEK64    = 21,
    SYS_IOCTL      = 22,
    NR_SYSCALL,
};

void do_syscall(struct trap_frame *tf);

#endif /* SYSCALL_H */
