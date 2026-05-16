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
    void               *kstack;      // kernel stack
    void               *ustack;      // user stack
    int                 wait_pid;    // waitpid 等待的 child pid
    bool                is_kernel;   // 擋 user stop/kill idle/shell
    struct list_head    rq_link;     // run queue / zombie list 共用
    struct list_head    child_link; // child 在 parent children list 的鏈結；parent 直接用 children list 頭
    struct list_head    children; // parent 的 children list 頭，child_link 才是實際鏈結
    struct list_head    all_link;
    unsigned int        signal_pending;
    unsigned long       signal_handlers[NSIG];
    struct trap_frame   signal_saved_tf;
    void               *signal_stack;
    bool                signal_handling;
    unsigned long       pgd_pa;        // user process 的 PGD（PA）；kernel thread = 0
    struct list_head    vma_list;      // mmap 區段，按 start 升冪
};

extern struct list_head all_threads;
extern struct thread *idle_thread;   // idle 兼任 init，承接 orphan child

void kthread_init(void);
struct thread* kthread_create(thread_func_t fn);
struct thread* kthread_bootstrap_idle(void);
struct thread* kthread_get_current(void);
int  alloc_pid(void);
void thread_exit(void);

extern void switch_to(struct thread* prev, struct thread* next);

#endif // THREAD_H
