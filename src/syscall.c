#include "syscall.h"
#include "uart.h"
#include "thread.h"
#include "exec.h"
#include "proc.h"
#include "video.h"
#include "signal.h"

static long sys_getpid(struct trap_frame *tf) {
    (void)tf;
    return kthread_get_current()->pid;
}

static long sys_uart_read(struct trap_frame *tf) {
    char *buf = (char *)tf->a0;
    long  n   = (long) tf->a1;
    for (long i = 0; i < n; i++) buf[i] = uart_getc();
    return n;
}

static long sys_uart_write(struct trap_frame *tf) {
    const char *buf = (const char *)tf->a0;
    long        n   = (long)        tf->a1;
    for (long i = 0; i < n; i++) uart_putc(buf[i]);
    return n;
}

// 成功不返回（直接 sret 到新 entry），失敗回 -1
static long sys_exec(struct trap_frame *tf) {
    return do_exec((const char *)tf->a0, tf);
}

// parent 拿 child pid，child 第一次跑時看到 a0 = 0
static long sys_fork(struct trap_frame *tf) {
    return do_fork(tf);
}

static long sys_waitpid(struct trap_frame *tf) {
    return do_waitpid((long)tf->a0);
}

static long sys_exit(struct trap_frame *tf) {
    do_exit((int)tf->a0);
    return 0;
}

static long sys_stop(struct trap_frame *tf) {
    return do_stop((long)tf->a0);
}

static long sys_display(struct trap_frame *tf) {
    video_bmp_display((unsigned int *)tf->a0, (int)tf->a1, (int)tf->a2);
    return 0;
}

static long sys_usleep(struct trap_frame *tf) {
    return do_usleep((unsigned int)tf->a0);
}

static long sys_signal(struct trap_frame *tf) {
    return do_signal((int)tf->a0, (unsigned long)tf->a1);
}

static long sys_kill(struct trap_frame *tf) {
    return do_kill((int)tf->a0, (int)tf->a1);
}

// switch 而非 function pointer table：self-relocation 後 .rodata 內絕對位址不 fix-up
void do_syscall(struct trap_frame *tf) {
    long nr = (long)tf->a7;
    long ret = -1;
    switch (nr) {
        case SYS_GETPID:     ret = sys_getpid(tf);     break;
        case SYS_UART_READ:  ret = sys_uart_read(tf);  break;
        case SYS_UART_WRITE: ret = sys_uart_write(tf); break;
        case SYS_EXEC:       ret = sys_exec(tf);       break;
        case SYS_FORK:       ret = sys_fork(tf);       break;
        case SYS_WAITPID:    ret = sys_waitpid(tf);    break;
        case SYS_EXIT:       ret = sys_exit(tf);       break;
        case SYS_STOP:       ret = sys_stop(tf);       break;
        case SYS_DISPLAY:    ret = sys_display(tf);    break;
        case SYS_USLEEP:     ret = sys_usleep(tf);     break;
        case SYS_SIGNAL:     ret = sys_signal(tf);     break;
        case SYS_KILL:       ret = sys_kill(tf);       break;
        // do_sigreturn 已整盤覆寫 *tf（含 a0），不能再走後面的 a0 寫回
        case SYS_SIGRETURN:  do_sigreturn(tf);         return;
        default:             ret = -1;                 break;
    }
    tf->a0 = (uint64_t)ret;
}
