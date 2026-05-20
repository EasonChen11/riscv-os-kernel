#include "proc.h"
#include "thread.h"
#include "schedule.h"
#include "buddy.h"
#include "dynalloc.h"
#include "irq.h"
#include "uart.h"
#include "string.h"
#include "timer.h"
#include "vm.h"
#include "mm.h"

extern void ret_from_exception(void);

// pid → thread；ZOMBIE 視為「已不存在」直接跳過，避免 stop/kill 對 reaping 中的目標下手
struct thread *find_pid(int pid) {
    struct list_head *p = all_threads.next;
    while (p != &all_threads) {
        struct thread *t = list_entry(p, struct thread, all_link);
        if (t->pid == pid && t->state != THREAD_ZOMBIE) return t;
        p = p->next;
    }
    return (void *)0;
}

// pid=-1 表示 reap 任意 child；找到的 child 必定 state == ZOMBIE
static struct thread *find_zombie_child(struct thread *parent, long pid) {
    struct list_head *p = parent->children.next;
    while (p != &parent->children) {
        struct thread *c = list_entry(p, struct thread, child_link);
        if (c->state == THREAD_ZOMBIE && (pid == -1 || c->pid == pid))
            return c;
        p = p->next;
    }
    return (void *)0;
}

// fork：parent 拿到 child pid，child 從 ret_from_exception 醒來看到 a0=0
// 沒 MMU 也沒 COW：實打實複製 kstack + ustack 各一個 page
long do_fork(struct trap_frame *parent_tf) {
    struct thread *p = kthread_get_current();
    struct thread *c = dynalloc_alloc(sizeof(*c));

    *c = *p;                                    // 淺拷貝大部分欄位（pid 等下面覆寫）
    c->pid   = alloc_pid();
    c->ppid  = p->pid;
    c->state = THREAD_RUNNING;
    c->wait_pid = -1;
    init_list_head(&c->rq_link);
    init_list_head(&c->child_link);
    init_list_head(&c->children);

    // signal_handlers[] 故意繼承（lab spec）；per-delivery state 清掉避免 double-free
    c->signal_pending  = 0;
    c->signal_handling = false;
    c->signal_stack    = (void *)0;

    // 1. kstack 整塊複製（含 parent 留下的 trap_frame 副本）
    c->kstack = buddy_alloc_pages(0);
    memcpy(c->kstack, p->kstack, KSTACK_SIZE);

    // 2. child trap_frame 在 child kstack 的同 offset
    unsigned long off = (unsigned long)parent_tf - (unsigned long)p->kstack;
    struct trap_frame *child_tf =
        (struct trap_frame *)((unsigned long)c->kstack + off);

    /* 3. lab6：deep-copy user pages via per-process PGD。
     *    parent / child user VA layout 完全一致，trap_frame.sp 不必修。
     *    kernel thread (pgd_pa=0) 不分配 user PGD，fork 後仍是 kernel thread。 */
    c->ustack = (void *)0;
    if (p->pgd_pa) {
        c->pgd_pa = alloc_pgd();
        /* Adv3：CoW share。parent / child 都改唯讀 + refcnt++，
         * 第一次 store 才走 do_page_fault → 配新頁 + memcpy。 */
        vm_copy_user_cow(c->pgd_pa, p->pgd_pa);
    } else {
        c->pgd_pa = 0;
    }
    vma_copy_list(&c->vma_list, &p->vma_list);
    /* vma_copy_list 給 child 新配 vma node，parent 那顆 stack_vma pointer 不再有效；
     * 走 child vma_list 找 end 同 parent stack_vma 那顆對位回來。 */
    c->stack_vma = (struct vma *)0;
    if (p->stack_vma) {
        struct list_head *q = c->vma_list.next;
        while (q != &c->vma_list) {
            struct vma *cv = list_entry(q, struct vma, link);
            if (cv->end == p->stack_vma->end) { c->stack_vma = cv; break; }
            q = q->next;
        }
    }

    // 4. child 的 fork 回傳值 = 0；同時 patch tp，避免 trap exit 從 frame load
    //    回 parent 的 tp（switch_to 雖然 mv tp, a1 過，ret_from_exception 又會
    //    從 trap_frame.tp 還原），導致 child 用 parent 的 thread pointer。
    child_tf->a0 = 0;
    child_tf->tp = (uint64_t)c;

    // 5. child 第一次被 schedule：跳 ret_from_exception，sp 指 trap_frame
    //    這樣 trap exit 把 GPR / CSR 從 child_tf ld 回來、sret 進 U-mode，
    //    跳過完整 do_trap 路徑（不該再 dispatch 一次同個 syscall）
    c->context.ra = (unsigned long)ret_from_exception;
    c->context.sp = (unsigned long)child_tf;

    // 6. parent ↔ child 連結 + 全域索引
    list_add_tail(&c->child_link, &p->children);
    list_add_tail(&c->all_link,   &all_threads);
    enqueue_runqueue(c);
    return c->pid;
}

// parent 死前把 children 全部過繼給 idle（兼任 init）。
// 前置：caller 在 syscall trap path，SIE=0。只搬 child_link 與 ppid，不動 child 的 state。
static void reparent_children_to_idle(struct thread *dying) {
    while (!list_empty(&dying->children)) {
        struct thread *c = list_entry(dying->children.next,
                                      struct thread, child_link);
        list_del(&c->child_link);
        c->ppid = idle_thread->pid;
        list_add_tail(&c->child_link, &idle_thread->children);
    }
}

// 將 t 標 ZOMBIE 並處理 parent 通知。caller 必須已把 t 從 runqueue 摘除。
// 前置：呼叫端在 syscall trap path，SIE=0，故不需 irq_save。
// 共用給 do_exit（t == me）與 do_stop（t == 別人）。
static void zombify(struct thread *t) {
    t->state = THREAD_ZOMBIE;
    reparent_children_to_idle(t);

    struct thread *p = find_pid(t->ppid);
    if (p && p->state == THREAD_WAITING
         && (p->wait_pid == -1 || p->wait_pid == t->pid)) {
        // parent 正在等 → 喚醒走 do_waitpid 路徑 reap（child_link 留著給它找）
        p->state = THREAD_RUNNING;
        enqueue_runqueue(p);
    } else if (!p || p == idle_thread) {
        // 沒人會等（parent 是 idle 或不存在）→ 自己進 zombie_list 給 idle 收
        list_del_init(&t->child_link);
        enqueue_zombie(t);
    }
    // 其餘：parent 活著但沒在 wait → 留 children list 等將來 waitpid
}

// 等 child exit；child 已 ZOMBIE 直接 reap，否則自己 sleep 到 do_exit 喚我
long do_waitpid(long pid) {
    struct thread *me = kthread_get_current();
    while (1) {
        unsigned long flags = irq_save();
        struct thread *z = find_zombie_child(me, pid);
        if (z) {
            long ret = z->pid;
            list_del(&z->child_link);
            enqueue_zombie(z);   // 交給 idle 收屍
            irq_restore(flags);
            return ret;
        }
        me->wait_pid = pid;
        me->state    = THREAD_WAITING;
        list_del(&me->rq_link);                          // 不在 run queue 才不會被誤選
        irq_restore(flags);
        schedule();                                      // child exit 時被喚起，迴圈再查一次
    }
}

// 將自己標 ZOMBIE 並從 run queue 摘除；若 parent 在 waitpid 等就喚醒。
// 注意不在這裡放進 zombie_list：留在 children list 給 parent waitpid 找；
// 沒 parent 等的（比如 root kthread foo）由下一次 schedule 內 list_del 後接手。
// 自己 exit：reparent children 給 idle、必要時通知 parent。
// 前置：syscall trap path，SIE=0。
void do_exit(int status) {
    (void)status;
    struct thread *me = kthread_get_current();

    list_del_init(&me->rq_link);
    zombify(me);

    schedule();   // 切走後不會回來
    while (1);    // 防呆
}

// 強制終止 pid：等同替它 do_exit。不能殺自己（殺自己用 exit）；不能殺 kernel thread。
// 前置：syscall trap path，SIE=0。
long do_stop(long pid) {
    struct thread *t = find_pid(pid);
    if (!t || t == kthread_get_current() || t->is_kernel) return -1;
    if (t->state == THREAD_RUNNING) {
        list_del(&t->rq_link);
    }
    // WAITING thread 已不在 run queue
    zombify(t);
    return 0;
}

// 用 pid 而非 thread* 當 timer arg：alloc_pid 嚴格遞增不重用，
// thread 被 stop+free 後 find_pid 找不到舊 pid，避免 stale pointer 假喚醒
static void sleep_wake(void* arg) {
    unsigned long flags = irq_save();
    int pid = (int)(unsigned long)arg;
    struct thread* t = find_pid(pid);
    if (!t || t->state != THREAD_WAITING) {
        irq_restore(flags);
        return;
    }
    t->state = THREAD_RUNNING;
    enqueue_runqueue(t);
    irq_restore(flags);
}

// 前置條件：caller SIE=0（SYS_USLEEP 由 trap entry 保證）；其他路徑要自己關 SIE
long do_usleep(unsigned int usec) {
    if (usec == 0) return 0;
    struct thread* me = kthread_get_current();
    unsigned long ticks = (unsigned long)usec * (timer_get_freq() / 1000000UL);
    add_timer_ticks(sleep_wake, (void*)(unsigned long)me->pid, ticks);

    me->state = THREAD_WAITING;
    list_del_init(&me->rq_link);

    schedule();   // 醒來時 sleep_wake 已把 me 塞回 run queue
    return 0;
}


