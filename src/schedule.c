#include "schedule.h"

static struct list_head runqueue;

void schedule_init(void) {
    init_list_head(&runqueue);
}

void enqueue_runqueue(struct thread* t) {
    list_add_tail(&t->rq_link, &runqueue);
}

void schedule(void) {
    struct thread* prev = kthread_get_current();

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
        schedule();
    }
}
