#include "thread.h"
#include "schedule.h"
#include "dynalloc.h"
#include "buddy.h"
#include "proc.h"

static int next_thread_id = 0;
struct list_head all_threads;

int alloc_pid(void) {
    return next_thread_id++;
}

void kthread_init(void) {
    init_list_head(&all_threads);
}

struct thread* kthread_create(thread_func_t fn) {
    struct thread* t = dynalloc_alloc(sizeof(*t));
    t->pid          = alloc_pid();
    t->ppid         = 0;                       // 由 main bootstrap 出來的 thread 沒 parent
    t->state        = THREAD_RUNNING;          // 不是 RUNNING 的話 schedule 不會 rotate、永遠不會被選到
    t->kstack       = buddy_alloc_pages(0);    // 必須配 stack；sp=0 第一次切過去 prologue 寫 stack 就 fault
    t->ustack       = (void *)0;               // 純 kernel thread；do_exec 第一次 swap 時才 lazy-alloc
    t->wait_pid     = -1;
    t->context.ra   = (unsigned long)fn;       // 第一次 switch_to 結尾 ret 會跳到這裡
    t->context.sp   = (unsigned long)t->kstack + KSTACK_SIZE;   // 棧頂（RISC-V stack 向下長）
    init_list_head(&t->rq_link);
    init_list_head(&t->child_link);
    init_list_head(&t->children);
    list_add_tail(&t->all_link, &all_threads);
    enqueue_runqueue(t);
    return t;
}

struct thread* kthread_get_current(void) {
    register struct thread* current asm("tp");
    return current;
}

// 一次性 bootstrap：建 idle thread + 把 tp 設過去；start_kernel 從此活在 thread 抽象裡
struct thread* kthread_bootstrap_idle(void) {
    extern void idle(void);
    struct thread* t = kthread_create(idle);
    asm volatile("mv tp, %0" :: "r"(t));
    return t;
}

// foo 等 kernel thread 用：自己跑完後丟給 do_exit 統一處理
// （do_exit 會喚醒等候 parent、把自己搬進 zombie_list、schedule 切走）
void thread_exit(void) {
    do_exit(0);
}
