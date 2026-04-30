#include "proc.h"
#include "thread.h"
#include "schedule.h"
#include "buddy.h"
#include "dynalloc.h"
#include "irq.h"
#include "uart.h"
#include "string.h"
#include "timer.h"

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

    /* signal_handlers[] 故意保留繼承（lab5.rst spec：fork 後 child 沿用 parent
     * 已註冊的 handler）。但 per-delivery 暫存欄位必須清掉：parent 若正在跑
     * signal handler 中途 fork（理論上 user 不會這樣寫，但防呆），淺拷貝會
     * 把 signal_handling=true / signal_stack=parent 的 stack ptr 帶過去；
     * 之後 child 第一次中斷會被誤判成「正在 handle」而跳過 deliver，且
     * sigreturn 時去 free parent 的 signal_stack 造成 double free。
     * 直接歸零，child 視為「未在處理任何 signal」。 */
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

    // 3. ustack 複製，修 child 的 user sp（parent sp 在 ustack 內的 offset 不變）
    if (p->ustack) {
        c->ustack = buddy_alloc_pages(0);
        memcpy(c->ustack, p->ustack, PAGE_SIZE);
        child_tf->sp = (unsigned long)c->ustack
                     + (parent_tf->sp - (unsigned long)p->ustack);
    } else {
        c->ustack = (void *)0;
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
        enqueue_zombie(me);
    }

    schedule();   // 切走後不會回來
    while (1);    // 防呆
}

// 強制終止 pid：標 ZOMBIE 並搬進 zombie_list；不能殺自己（殺自己用 exit）
long do_stop(long pid) {
    struct thread *t = find_pid(pid);
    if (!t || t == kthread_get_current()) return -1;
    if (t->is_kernel) return -1;   // 擋下殺 idle / kernel-side shell 之類核心 thread
    if (t->state == THREAD_RUNNING) {
        list_del(&t->rq_link);
    }
    // WAITING thread 已不在 run queue，直接搬就好
    t->state = THREAD_ZOMBIE;
    enqueue_zombie(t);
    // 從 child list 卸下（避免 parent waitpid 看到一個 ZOMBIE 但無人 reap 的怪態）
    // list_del_init 因為 child_link 之後不會再加進任何 list，留著 stale prev/next 是雷
    list_del_init(&t->child_link);
    return 0;
}

// usleep：把自己標 WAITING、排 timer，到期 callback 把自己塞回 run queue。
// 比起 busy-wait usleep 更省 CPU；schedule 切走後別人能跑。
//
// 喚醒 handle 直接用 pid 整數塞進 timer arg，callback 不再 deref 任何指標。
// 理由：thread struct 由 dynalloc 配置，timer queue 內若仍以 pointer 形式
// 持有 thread 內部欄位（之前的 &me->sleep_ctx 設計），thread 被 stop+free
// 並 chunk 被新 thread 接走後，stale pointer 會落在新 thread 的合法欄位上，
// find_pid 取到的是新 thread 的 pid → 假喚醒。改用 pid 整數後，alloc_pid
// 嚴格遞增不重用，find_pid 找不到舊 pid 就直接 drop，避免整類 race。
static void sleep_wake(void* arg) {
    unsigned long flags = irq_save();
    int pid = (int)(unsigned long)arg;
    struct thread* t = find_pid(pid);
    /* t == NULL：thread 已 zombie 或被 kill_zombies 從 all_threads 拿掉。
     * t->state != THREAD_WAITING：被 stop 變 ZOMBIE，或被別人提前喚醒。
     * 兩種都不該再標 RUNNING + enqueue。 */
    if (!t || t->state != THREAD_WAITING) {
        irq_restore(flags);
        return;
    }
    t->state = THREAD_RUNNING;
    enqueue_runqueue(t);
    irq_restore(flags);
}

// 前置條件：caller 必須在 SIE=0 環境（目前唯一路徑為 SYS_USLEEP，trap entry
// 已自動清 SIE）。下面 state + list_del 兩行不再 irq_save，依賴此 invariant。
// 若未來新增 kthread 直接呼叫 do_usleep 的路徑，記得補回 irq_save 或在 caller
// 側自行關 SIE，否則 preempt timer 在 RMW 中間 fire 會壞 list。
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


