#include "schedule.h"
#include "buddy.h"
#include "dynalloc.h"
#include "uart.h"

static struct list_head runqueue;
struct list_head zombie_list;          // proc.c 也會 enqueue（waitpid reap、stop）

void schedule_init(void) {
    init_list_head(&runqueue);
    init_list_head(&zombie_list);
}

void enqueue_runqueue(struct thread* t) {
    list_add_tail(&t->rq_link, &runqueue);
}

void enqueue_zombie(struct thread* t) {
    // 三步原子：rq_link 一個欄位在兩條互斥 list 間搬家（runqueue → zombie_list）
    list_del(&t->rq_link);
    t->state = THREAD_ZOMBIE;
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
