#include "signal.h"
#include "thread.h"
#include "proc.h"
#include "uart.h"
#include "string.h"

extern const char sigreturn_trampoline[];
extern const char sigreturn_trampoline_end[];

// trap exit 前處理 pending signal；改 tf 讓 user 醒來跑 handler。
// !me 守衛擋 boot 早期 tp 未初始化時 trap 進來的 deref。
void deliver_signal(struct trap_frame *tf) {
    struct thread *me = kthread_get_current();
    if (!me) return;
    if (me->is_kernel) return;
    if (me->signal_pending == 0) return;
    if (me->signal_handling) return;       // nested 防護

    for (int s = 0; s < NSIG; s++) {
        if (!(me->signal_pending & (1u << s))) continue;
        me->signal_pending &= ~(1u << s);

        unsigned long h = me->signal_handlers[s];
        if (h == 0) {
            if (s == SIGTERM) {
                uart_puts("[signal] default SIGTERM -> do_exit\n");
                do_exit(0);
            }
            return;
        }

        me->signal_handling = true;
        me->signal_saved_tf = *tf;

        /* trampoline 壓在 user 自己的 stack 上：
         * SUM=1 允許 kernel 直接寫 user VA。
         * 若該頁尚未 map（stack 跨頁），memcpy 觸發 S-mode store fault，
         * do_page_fault 透過 try_expand_stack 自動補頁，不需要額外處理。 */
        unsigned long tramp_size = sigreturn_trampoline_end - sigreturn_trampoline;
        unsigned long tramp_user = (tf->sp - tramp_size) & ~0xfUL;
        memcpy((void *)tramp_user, sigreturn_trampoline, tramp_size);
        asm volatile("fence.i" ::: "memory");

        tf->sepc = h;
        tf->sp   = tramp_user;
        tf->ra   = tramp_user;
        return;
    }
}

long do_signal(int signum, unsigned long handler) {
    if (signum < 0 || signum >= NSIG) return -1;
    kthread_get_current()->signal_handlers[signum] = handler;
    return 0;
}

long do_kill(int pid, int signum) {
    if (signum < 0 || signum >= NSIG) return -1;
    if (pid <= 0) return -1;
    struct thread *t = find_pid(pid);
    if (!t) return -1;
    if (t->is_kernel) return -1;
    t->signal_pending |= (1u << signum);
    return 0;
}

void do_sigreturn(struct trap_frame *tf) {
    struct thread *me = kthread_get_current();
    if (!me || !me->signal_handling) return;
    *tf = me->signal_saved_tf;
    me->signal_handling = false;
}
