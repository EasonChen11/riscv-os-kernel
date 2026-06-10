#include "syscall.h"
#include "uart.h"
#include "thread.h"
#include "exec.h"
#include "proc.h"
#include "video.h"
#include "signal.h"
#include "mm.h"
#include "vfs.h"

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

static long sys_mmap(struct trap_frame *tf) {
    return do_mmap(kthread_get_current(),
                   (void *)tf->a0, tf->a1, (int)tf->a2, (int)tf->a3);
}

// ---- VFS syscalls（user buffer 直接存取：syscall 期間 satp 仍 user PGD + SUM=1）----
static long sys_open(struct trap_frame *tf) {
    struct file *f;
    if (vfs_open((const char *)tf->a0, (int)tf->a1, &f) != E_OK) return -1;
    int fd = fd_alloc(kthread_get_current(), f);
    if (fd < 0) { vfs_close(f); return -1; }   // fd table 滿，回收 handle
    return fd;
}

static long sys_close(struct trap_frame *tf) {
    struct thread *t = kthread_get_current();
    struct file *f = fd_get(t, (int)tf->a0);
    if (!f) return -1;
    vfs_close(f);
    t->fd_table[(int)tf->a0] = (struct file *)0;
    return 0;
}

static long sys_read(struct trap_frame *tf) {
    struct file *f = fd_get(kthread_get_current(), (int)tf->a0);
    if (!f) return -1;
    return vfs_read(f, (void *)tf->a1, (uint64_t)tf->a2);
}

static long sys_write(struct trap_frame *tf) {
    struct file *f = fd_get(kthread_get_current(), (int)tf->a0);
    if (!f) return -1;
    return vfs_write(f, (const void *)tf->a1, (uint64_t)tf->a2);
}

static long sys_mkdir(struct trap_frame *tf) {
    return vfs_mkdir((const char *)tf->a0);   // a1 = mode，本 lab 忽略
}

static long sys_mount(struct trap_frame *tf) {
    // mount(src, target, fs, flags, data)：a0 src(忽略) a1 target a2 fs
    return vfs_mount((const char *)tf->a1, (const char *)tf->a2);
}

static long sys_chdir(struct trap_frame *tf) {
    struct vnode *v;
    if (vfs_lookup((const char *)tf->a0, &v) != E_OK) return -1;
    v = vnode_follow_mount(v);
    kthread_get_current()->cwd = v;
    return 0;
}

static long sys_lseek64(struct trap_frame *tf) {
    struct file *f = fd_get(kthread_get_current(), (int)tf->a0);
    if (!f || !f->f_ops->lseek64) return -1;
    return f->f_ops->lseek64(f, (long)tf->a1, (int)tf->a2);
}

static long sys_ioctl(struct trap_frame *tf) {
    struct file *f = fd_get(kthread_get_current(), (int)tf->a0);
    if (!f || !f->f_ops->ioctl) return -1;
    return f->f_ops->ioctl(f, (unsigned long)tf->a1, (void *)tf->a2);
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
        case SYS_MMAP:       ret = sys_mmap(tf);        break;
        case SYS_OPEN:       ret = sys_open(tf);        break;
        case SYS_CLOSE:      ret = sys_close(tf);       break;
        case SYS_READ:       ret = sys_read(tf);        break;
        case SYS_WRITE:      ret = sys_write(tf);       break;
        case SYS_MKDIR:      ret = sys_mkdir(tf);       break;
        case SYS_MOUNT:      ret = sys_mount(tf);       break;
        case SYS_CHDIR:      ret = sys_chdir(tf);       break;
        case SYS_LSEEK64:    ret = sys_lseek64(tf);     break;
        case SYS_IOCTL:      ret = sys_ioctl(tf);       break;
        // do_sigreturn 已整盤覆寫 *tf（含 a0），不能再走後面的 a0 寫回
        case SYS_SIGRETURN:  do_sigreturn(tf);         return;
        default:             ret = -1;                 break;
    }
    tf->a0 = (uint64_t)ret;
}
