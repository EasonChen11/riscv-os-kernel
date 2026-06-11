#include "syscall.h"
#include "uart.h"
#include "thread.h"
#include "exec.h"
#include "proc.h"
#include "video.h"
#include "signal.h"
#include "mm.h"
#include "vfs.h"
#include "pgtable.h"

/* user 指標範圍驗證：buf 必須完整落在 user 空間（低於 PAGE_OFFSET），
 * 同時防止 p+len 整數溢位繞過檢查。len=0 視為合法。 */
static int valid_user_buf(const void* buf, uint64_t len) {
    unsigned long p = (unsigned long)buf;
    if (len == 0) return 1;
    if (p >= PAGE_OFFSET) return 0;           /* buf 起點已在 kernel 空間 */
    if (p + len < p) return 0;               /* 整數溢位 */
    if (p + len > PAGE_OFFSET) return 0;     /* buf 尾端跨進 kernel 空間 */
    return 1;
}

static long sys_getpid(struct trap_frame *tf) {
    (void)tf;
    return kthread_get_current()->pid;
}

// uart_read(buf, len)
static long sys_uart_read(struct trap_frame *tf) {
    char *buf = (char *)tf->a0;   // a0=buf
    long  n   = (long) tf->a1;   // a1=len
    if (!valid_user_buf(buf, (uint64_t)n)) return -1;
    for (long i = 0; i < n; i++) buf[i] = uart_getc();
    return n;
}

// uart_write(buf, len)
static long sys_uart_write(struct trap_frame *tf) {
    const char *buf = (const char *)tf->a0;   // a0=buf
    long        n   = (long)        tf->a1;   // a1=len
    if (!valid_user_buf(buf, (uint64_t)n)) return -1;
    for (long i = 0; i < n; i++) uart_putc(buf[i]);
    return n;
}

// exec(path)
static long sys_exec(struct trap_frame *tf) {
    return do_exec((const char *)tf->a0, tf);   // a0=path；成功不返回
}

// fork()：parent 拿 child pid，child 第一次跑時看到 a0=0
static long sys_fork(struct trap_frame *tf) {
    return do_fork(tf);
}

// waitpid(pid)
static long sys_waitpid(struct trap_frame *tf) {
    return do_waitpid((long)tf->a0);   // a0=pid
}

// exit(status)
static long sys_exit(struct trap_frame *tf) {
    do_exit((int)tf->a0);   // a0=status
    return 0;
}

// stop(pid)
static long sys_stop(struct trap_frame *tf) {
    return do_stop((long)tf->a0);   // a0=pid
}

// display(bmp_ptr, x, y)
static long sys_display(struct trap_frame *tf) {
    video_bmp_display((unsigned int *)tf->a0, (int)tf->a1, (int)tf->a2);
    return 0;   // a0=bmp_ptr  a1=x  a2=y
}

// usleep(us)
static long sys_usleep(struct trap_frame *tf) {
    return do_usleep((unsigned int)tf->a0);   // a0=microseconds
}

// signal(signum, handler)
static long sys_signal(struct trap_frame *tf) {
    return do_signal((int)tf->a0, (unsigned long)tf->a1);   // a0=signum  a1=handler
}

// kill(pid, signum)
static long sys_kill(struct trap_frame *tf) {
    return do_kill((int)tf->a0, (int)tf->a1);   // a0=pid  a1=signum
}

// mmap(addr, len, prot, flags)
static long sys_mmap(struct trap_frame *tf) {
    return do_mmap(kthread_get_current(),
                   (void *)tf->a0, tf->a1, (int)tf->a2, (int)tf->a3);
    // a0=addr  a1=len  a2=prot  a3=flags
}

// ---- VFS syscalls（user buffer 直接存取：syscall 期間 satp 仍 user PGD + SUM=1）----

// open(path, flags)
static long sys_open(struct trap_frame *tf) {
    struct file *f;
    if (vfs_open((const char *)tf->a0, (int)tf->a1, &f) != E_OK) return -1;
    // a0=path  a1=flags
    int fd = fd_alloc(kthread_get_current(), f);
    if (fd < 0) { vfs_close(f); return -1; }
    return fd;
}

// close(fd)
static long sys_close(struct trap_frame *tf) {
    struct thread *t = kthread_get_current();
    struct file *f = fd_get(t, (int)tf->a0);   // a0=fd
    if (!f) return -1;
    vfs_close(f);
    t->fd_table[(int)tf->a0] = (struct file *)0;
    return 0;
}

// read(fd, buf, len)
static long sys_read(struct trap_frame *tf) {
    struct file *f = fd_get(kthread_get_current(), (int)tf->a0);
    // a0=fd  a1=buf  a2=len
    if (!f) return -1;
    if (!valid_user_buf((void *)tf->a1, (uint64_t)tf->a2)) return -1;
    return vfs_read(f, (void *)tf->a1, (uint64_t)tf->a2);
}

// write(fd, buf, len)
static long sys_write(struct trap_frame *tf) {
    struct file *f = fd_get(kthread_get_current(), (int)tf->a0);
    // a0=fd  a1=buf  a2=len
    if (!f) return -1;
    if (!valid_user_buf((const void *)tf->a1, (uint64_t)tf->a2)) return -1;
    return vfs_write(f, (const void *)tf->a1, (uint64_t)tf->a2);
}

// mkdir(path)
static long sys_mkdir(struct trap_frame *tf) {
    return vfs_mkdir((const char *)tf->a0);   // a0=path；a1=mode 本 lab 忽略
}

// mount(src, target, fstype, flags, data)
static long sys_mount(struct trap_frame *tf) {
    return vfs_mount((const char *)tf->a1, (const char *)tf->a2);
    // a0=src(忽略)  a1=target  a2=fstype  a3=flags(忽略)  a4=data(忽略)
}

// chdir(path)
static long sys_chdir(struct trap_frame *tf) {
    struct vnode *v;
    if (vfs_lookup((const char *)tf->a0, &v) != E_OK) return -1;   // a0=path
    v = vnode_follow_mount(v);
    kthread_get_current()->cwd = v;
    return 0;
}

// lseek64(fd, offset, whence)
static long sys_lseek64(struct trap_frame *tf) {
    struct file *f = fd_get(kthread_get_current(), (int)tf->a0);
    // a0=fd  a1=offset  a2=whence
    if (!f) return -1;
    return f->f_ops->lseek64(f, (long)tf->a1, (int)tf->a2);
}

// ioctl(fd, request, arg)
static long sys_ioctl(struct trap_frame *tf) {
    struct file *f = fd_get(kthread_get_current(), (int)tf->a0);
    // a0=fd  a1=request  a2=arg
    if (!f) return -1;
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
