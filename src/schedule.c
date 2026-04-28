#include "schedule.h"
#include "buddy.h"
#include "dynalloc.h"
#include "uart.h"
#include "timer.h"
#include "task.h"

static struct list_head runqueue;
struct list_head zombie_list;          // proc.c 也會 enqueue（waitpid reap、stop）

volatile int need_resched = 0;
#define PREEMPT_HZ 32                  // 1/32 秒一次 schedule check

void schedule_init(void) {
    init_list_head(&runqueue);
    init_list_head(&zombie_list);
}

void enqueue_runqueue(struct thread* t) {
    list_add_tail(&t->rq_link, &runqueue);
}

// 純 add：把 t 加進 zombie_list 等 idle 收屍。
// caller 負責先 detach（list_del/list_del_init）並設 t->state = THREAD_ZOMBIE。
// 拆成單一職責讓 do_exit / do_waitpid / do_stop 都能用同一個 enqueue 入口。
void enqueue_zombie(struct thread* t) {
    list_add_tail(&t->rq_link, &zombie_list);
}

// 由 idle 呼叫；thread 不能 free 自己（kstack 還在用），必須切走後由別人代清
void kill_zombies(void) {
    while (!list_empty(&zombie_list)) {
        struct thread* z = list_entry(zombie_list.next, struct thread, rq_link);
        list_del(&z->rq_link);
        list_del(&z->all_link);          // 從 all_threads 摘下，find_pid 不再回它
        uart_puts("[zombie freed pid=");
        uart_hex(z->pid);
        uart_puts("]\n");
        buddy_free_pages(z->kstack);
        if (z->ustack) buddy_free_pages(z->ustack);
        dynalloc_free(z);
    }
}

void schedule(void) {
    struct thread* prev = kthread_get_current();
    if (!prev) return;   // boot 早期 tp 還沒被 kthread_bootstrap_idle 設好；保險早退

    /* RUNNING：rotate 自己到 queue 尾（round-robin）。
       非 RUNNING（ZOMBIE/WAITING）表示 rq_link 已被別處移走，不該再碰。 */
    if (prev->state == THREAD_RUNNING) {
        list_del(&prev->rq_link);
        list_add_tail(&prev->rq_link, &runqueue);
    }

    /* runqueue 不該空（至少 idle 在裡頭）；保險早退 */
    if (list_empty(&runqueue)) return;

    struct thread* next = list_entry(runqueue.next, struct thread, rq_link);

    /* rotate 完 first 就是 prev 自己 ⇒ queue 只剩自己一個 */
    if (next == prev) return;

    switch_to(prev, next);
}

void idle(void) {
    while (1) {
        kill_zombies();   // idle 是唯一保證會被排到的 thread，當 reaper 不會餓到
        schedule();
    }
}

// preempt 心跳：跑在 task context (SIE=1)。先自我重排下一拍，再標 need_resched
// 讓本次 trap exit 前 do_trap 會呼叫 schedule()。不直接 schedule 的原因：
// 1) cb 在 task context 內 schedule 會切走 task context，drain_tasks 結束點亂掉
// 2) 統一在 trap exit 一處呼叫 schedule，避免散落在多個 ISR / cb 內
static void preempt_tick_cb(void* arg) {
    (void)arg;
    add_timer_ticks(preempt_tick_cb, (void*)0, timer_get_freq() / PREEMPT_HZ);
    need_resched = 1;
}

void start_preempt(void) {
    add_timer_ticks(preempt_tick_cb, (void*)0, timer_get_freq() / PREEMPT_HZ);
}
