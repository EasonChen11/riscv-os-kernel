#ifndef THREAD_H
#define THREAD_H

#include <stdbool.h>
#include "list.h"
#include "trap.h"
#include "signal.h"

#define KSTACK_SIZE (1 << 12) // 4KB kernel stack per thread

typedef void (*thread_func_t)(void);

enum thread_state {
    THREAD_RUNNING,
    THREAD_WAITING,
    THREAD_ZOMBIE,
};

struct cpu_context {
    unsigned long ra;
    unsigned long sp;
    unsigned long s[12];
};

struct thread {
    struct cpu_context  context;     // 必須第一個：switch_to 用 byte offset
    enum thread_state   state;
    int                 pid;
    int                 ppid;
    void               *kstack;
    void               *ustack;      // 純 kernel thread 為 NULL
    int                 wait_pid;    // -1 = 任意 child
    bool                is_kernel;   // 擋 user stop/kill idle/shell
    struct list_head    rq_link;     // run queue / zombie list 共用
    struct list_head    child_link;
    struct list_head    children;
    struct list_head    all_link;
    unsigned int        signal_pending;
    unsigned long       signal_handlers[NSIG];
    struct trap_frame   signal_saved_tf;
    void               *signal_stack;
    bool                signal_handling;
};

extern struct list_head all_threads;

void kthread_init(void);
struct thread* kthread_create(thread_func_t fn);
struct thread* kthread_bootstrap_idle(void);
struct thread* kthread_get_current(void);
int  alloc_pid(void);
void thread_exit(void);

extern void switch_to(struct thread* prev, struct thread* next);

#endif // THREAD_H
