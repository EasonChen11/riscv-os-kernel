#include "proc.h"
#include "thread.h"
#include "schedule.h"
#include "buddy.h"
#include "dynalloc.h"
#include "irq.h"
#include "uart.h"
#include "string.h"

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

    // 1. kstack 整塊複製（含 parent 留下的 trap_frame 副本）
    c->kstack = buddy_alloc_pages(0);
    memcpy(c->kstack, p->kstack, KSTACK_SIZE);

    // 2. child trap_frame 在 child kstack 的同 offset
    unsigned long off = (unsigned long)parent_tf - (unsigned long)p->kstack;
    struct trap_frame *child_tf =
        (struct trap_frame *)((unsigned long)c->kstack + off);

    // 3. ustack 複製，修 child 的 user sp（parent sp 在 ustack 內的 offset 不變）
    if (p->ustack) {
        c->ustack = buddy_alloc_pages(0);
        memcpy(c->ustack, p->ustack, PAGE_SIZE);
        child_tf->sp = (unsigned long)c->ustack
                     + (parent_tf->sp - (unsigned long)p->ustack);
    } else {
        c->ustack = (void *)0;
    }

    // 4. child 的 fork 回傳值 = 0
    child_tf->a0 = 0;

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

// 等 child exit；child 已 ZOMBIE 直接 reap，否則自己 sleep 到 do_exit 喚我
long do_waitpid(long pid) {
    struct thread *me = kthread_get_current();
    while (1) {
        unsigned long flags = irq_save();
        struct thread *z = find_zombie_child(me, pid);
        if (z) {
            long ret = z->pid;
            list_del(&z->child_link);
            list_add_tail(&z->rq_link, &zombie_list);   // 交給 idle 收屍
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
void do_exit(int status) {
    (void)status;
    struct thread *me = kthread_get_current();

    me->state = THREAD_ZOMBIE;
    list_del_init(&me->rq_link);    // init 是為了之後若被加進 zombie_list（含 list_del 路徑）安全

    // 喚醒 waiting parent（若條件符合）
    struct thread *p = find_pid(me->ppid);
    if (p && p->state == THREAD_WAITING
         && (p->wait_pid == -1 || p->wait_pid == me->pid)) {
        p->state = THREAD_RUNNING;
        enqueue_runqueue(p);
    }

    // 沒 parent 認領的 zombie：直接丟 zombie_list 讓 idle 收
    if (!p) {
        list_add_tail(&me->rq_link, &zombie_list);
    }

    schedule();   // 切走後不會回來
    while (1);    // 防呆
}

// 強制終止 pid：標 ZOMBIE 並搬進 zombie_list；不能殺自己（殺自己用 exit）
long do_stop(long pid) {
    struct thread *t = find_pid(pid);
    if (!t || t == kthread_get_current()) return -1;
    if (t->state == THREAD_RUNNING) {
        list_del(&t->rq_link);
    }
    // WAITING thread 已不在 run queue，直接搬就好
    t->state = THREAD_ZOMBIE;
    list_add_tail(&t->rq_link, &zombie_list);
    // 從 child list 卸下（避免 parent waitpid 看到一個 ZOMBIE 但無人 reap 的怪態）
    list_del(&t->child_link);
    init_list_head(&t->child_link);
    return 0;
}
