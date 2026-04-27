#include "schedule.h"
#include "thread.h"
#include "list.h"

static struct list_head runqueue;

void schedule_init(void) {
    init_list_head(&runqueue);
}

void enqueue_runqueue(struct thread* t) {
    list_add_tail(&t->rq_link, &runqueue);
}

void schedule(void) {
    struct thread* current = kthread_get_current();

    /* current 永遠在 queue 中：直接走 next，碰到 sentinel 就 wrap 到 head */
    struct list_head* next_link = current->rq_link.next;
    if (next_link == &runqueue) next_link = runqueue.next;

    /* queue 只有 current 自己 ⇒ wrap 後又指回自己 */
    if (next_link == &current->rq_link) return;

    struct thread* next = list_entry(next_link, struct thread, rq_link);
    switch_to(current, next);
}

void idle(void) {
    while (1) {
        schedule();
    }
}
