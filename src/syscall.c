#include "syscall.h"
#include "uart.h"
#include "thread.h"

typedef long (*syscall_fn_t)(struct trap_frame *);

// SYS_GETPID：回傳當前 thread 的 pid（kthread_create 已分配遞增 id）
static long sys_getpid(struct trap_frame *tf) {
    (void)tf;
    return kthread_get_current()->pid;
}

// SYS_UART_READ：阻塞讀 a1 bytes 到 a0；user-supplied buffer 直接寫
// 沒 MMU、所有位址 kernel 直接可達；之後若加 ring buffer 再換實作
static long sys_uart_read(struct trap_frame *tf) {
    char *buf = (char *)tf->a0;
    long  n   = (long) tf->a1;
    for (long i = 0; i < n; i++) buf[i] = uart_getc();
    return n;
}

// SYS_UART_WRITE：把 a1 bytes 從 a0 推到 UART；polling/IRQ 共用 uart_putc
static long sys_uart_write(struct trap_frame *tf) {
    const char *buf = (const char *)tf->a0;
    long        n   = (long)        tf->a1;
    for (long i = 0; i < n; i++) uart_putc(buf[i]);
    return n;
}

// 預先佔好 NR_SYSCALL 槽；NULL 槽位由 dispatcher 統一回 -1，
// 後續 commit 加 handler 時直接 [SYS_FORK] = sys_fork 即可
static const syscall_fn_t syscall_table[NR_SYSCALL] = {
    [SYS_GETPID]     = sys_getpid,
    [SYS_UART_READ]  = sys_uart_read,
    [SYS_UART_WRITE] = sys_uart_write,
};

void do_syscall(struct trap_frame *tf) {
    long nr = (long)tf->a7;
    if (nr < 0 || nr >= NR_SYSCALL || !syscall_table[nr]) {
        tf->a0 = (uint64_t)-1L;
        return;
    }
    tf->a0 = (uint64_t)syscall_table[nr](tf);
}
