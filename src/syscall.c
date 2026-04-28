#include "syscall.h"
#include "uart.h"
#include "thread.h"
#include "exec.h"
#include "proc.h"
#include "video.h"

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

// SYS_EXEC：把當前 U-mode process 換成 path 指定的 image
// 成功不回（trap exit 直接 sret 到新 entry），失敗回 -1
static long sys_exec(struct trap_frame *tf) {
    return do_exec((const char *)tf->a0, tf);
}

// SYS_FORK：parent 拿 child pid，child 第一次跑時看到 a0=0
static long sys_fork(struct trap_frame *tf) {
    return do_fork(tf);
}

// SYS_WAITPID：sleep 等到 child exit；reap 成功回 child pid
static long sys_waitpid(struct trap_frame *tf) {
    return do_waitpid((long)tf->a0);
}

// SYS_EXIT：自己標 ZOMBIE 並切走，不返回
static long sys_exit(struct trap_frame *tf) {
    do_exit((int)tf->a0);
    return 0;
}

// SYS_STOP：強制終止 pid，不能殺自己
static long sys_stop(struct trap_frame *tf) {
    return do_stop((long)tf->a0);
}

// SYS_DISPLAY：把 user 提供的 BMP（XRGB8888）置中拷進 framebuffer
static long sys_display(struct trap_frame *tf) {
    video_bmp_display((unsigned int *)tf->a0, (int)tf->a1, (int)tf->a2);
    return 0;
}

// SYS_USLEEP：阻塞 usec 微秒；期間 thread 不在 run queue，CPU 讓給別人
static long sys_usleep(struct trap_frame *tf) {
    return do_usleep((unsigned int)tf->a0);
}

// switch dispatcher：避免 function pointer table 在 .rodata 留絕對位址，
// kernel self-relocation 後 table entry 仍指向 link 時舊位置
void do_syscall(struct trap_frame *tf) {
    long nr = (long)tf->a7;
    long ret;
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
        default:             ret = -1;                 break;
    }
    tf->a0 = (uint64_t)ret;
}
