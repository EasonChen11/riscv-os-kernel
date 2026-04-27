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
    t->state        = THREAD_RUNNING;
    t->kstack       = buddy_alloc_pages(0);
    t->context.ra   = (unsigned long)fn;
    t->context.sp   = (unsigned long)t->kstack + KSTACK_SIZE;
    enqueue_runqueue(t);
    return t;
}

struct thread* kthread_get_current(void) {
    register struct thread* current asm("tp");
    return current;
}
