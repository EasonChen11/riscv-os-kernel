#include "thread.h"
#include "schedule.h"
#include "dynalloc.h"
#include "buddy.h"

static int next_thread_id = 0;

void kthread_init(void) {
}

struct thread* kthread_create(thread_func_t fn) {
    struct thread* t = dynalloc_alloc(sizeof(*t));
    t->pid          = next_thread_id++;
    t->state        = THREAD_RUNNING;          // 不是 RUNNING 的話 schedule 不會 rotate、永遠不會被選到
    t->kstack       = buddy_alloc_pages(0);    // 必須配 stack；sp=0 第一次切過去 prologue 寫 stack 就 fault
    t->ustack       = (void *)0;               // 純 kernel thread；do_exec 第一次 swap 時才 lazy-alloc
    t->context.ra   = (unsigned long)fn;       // 第一次 switch_to 結尾 ret 會跳到這裡
    t->context.sp   = (unsigned long)t->kstack + KSTACK_SIZE;   // 棧頂（RISC-V stack 向下長）
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

void thread_exit(void) {
    enqueue_zombie(kthread_get_current());
    schedule();      /* 切走後不會回來——current 已是 ZOMBIE，schedule 不會再選到它 */
    while (1);       /* 防呆：萬一 schedule 莫名其妙 return */
}
