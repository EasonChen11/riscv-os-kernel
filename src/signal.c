#include "signal.h"
#include "thread.h"
#include "proc.h"
#include "uart.h"
#include "buddy.h"
#include "string.h"
#include "mm.h"
#include "vm.h"
#include "pgtable.h"

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

        me->signal_handling   = true;
        me->signal_saved_tf   = *tf;
        me->signal_stack      = buddy_alloc_pages(0);

        unsigned long tramp_size = sigreturn_trampoline_end - sigreturn_trampoline;
        /* kernel VA：用來 memcpy trampoline */
        unsigned long kern_top  = (unsigned long)me->signal_stack + PAGE_SIZE;
        unsigned long kern_tramp = (kern_top - tramp_size) & ~0xfUL;
        memcpy((void *)kern_tramp, sigreturn_trampoline, tramp_size);
        asm volatile("fence.i" ::: "memory");

        /* 把這頁 map 進 user page table，讓 user mode 能執行 trampoline */
        map_pages(me->pgd_pa, USER_SIGNAL_STACK_VA, PAGE_SIZE,
                  virt_to_phys((unsigned long)me->signal_stack), PROT_USER_RWX);

        /* user VA：sepc/sp/ra 都要用 user VA */
        unsigned long user_top  = USER_SIGNAL_STACK_VA + PAGE_SIZE;
        unsigned long user_tramp = user_top - (kern_top - kern_tramp);

        tf->sepc = h;
        tf->sp   = user_tramp;
        tf->ra   = user_tramp;
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

// dispatcher 必須在 *tf 整盤覆寫後 return，避免 a0 被覆蓋
void do_sigreturn(struct trap_frame *tf) {
    struct thread *me = kthread_get_current();
    if (!me || !me->signal_handling) return;
    if (me->signal_stack) {
        vm_unmap_page(me->pgd_pa, USER_SIGNAL_STACK_VA);
        buddy_free_pages(me->signal_stack);
        me->signal_stack = (void *)0;
    }
    *tf = me->signal_saved_tf;
    me->signal_handling = false;
}
