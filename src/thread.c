#include "thread.h"
#include "schedule.h"
#include "dynalloc.h"
#include "buddy.h"
#include "proc.h"
#include "mm.h"

static int next_thread_id = 0;
struct list_head all_threads;
struct thread *idle_thread;

int alloc_pid(void) {
    return next_thread_id++;
}

void kthread_init(void) {
    init_list_head(&all_threads);
}

struct thread* kthread_create(thread_func_t fn) {
    struct thread* t = dynalloc_alloc(sizeof(*t));
    t->pid          = alloc_pid();
    t->ppid         = 0;
    t->state        = THREAD_RUNNING;     // 非 RUNNING 不會被 schedule 選到
    t->kstack       = buddy_alloc_pages(0);
    t->wait_pid     = -1;
    t->is_kernel    = true;               // do_exec 切 user image 後翻 false
    t->context.ra   = (unsigned long)fn;
    t->context.sp   = (unsigned long)t->kstack + KSTACK_SIZE;
    init_list_head(&t->rq_link);
    init_list_head(&t->child_link);
    init_list_head(&t->children);
    list_add_tail(&t->all_link, &all_threads);
    t->signal_pending = 0;
    for (int i = 0; i < NSIG; i++) t->signal_handlers[i] = 0;
    t->signal_handling = false;
    t->pgd_pa          = 0;   /* kernel thread；exec 之後 user thread 才設 */
    vma_init_list(&t->vma_list);
    t->stack_vma = (struct vma *)0;
    enqueue_runqueue(t);
    return t;
}

struct thread* kthread_get_current(void) {
    register struct thread* current asm("tp");
    return current;
}

struct thread* kthread_bootstrap_idle(void) {
    extern void idle(void);
    struct thread* t = kthread_create(idle);
    idle_thread = t;                  // idle 永遠存活、is_kernel=true、不會 WAITING
    asm volatile("mv tp, %0" :: "r"(t));
    return t;
}

void thread_exit(void) {
    do_exit(0);
}
