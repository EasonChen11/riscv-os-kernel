#ifndef THREAD_H
#define THREAD_H

#include <stdbool.h>
#include "list.h"
#include "trap.h"

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
    struct cpu_context  context;     // 必須是第一個成員：switch_to 用 byte offset 直接 sd/ld
    enum thread_state   state;
    int                 pid;
    int                 ppid;        // fork 設定；root thread 為 0
    void               *kstack;
    void               *ustack;      // U-mode stack；純 kernel thread 為 NULL
    int                 wait_pid;    // waitpid 等的 child pid；-1 表示任意
    bool                is_kernel;   // true=kernel thread；保護 idle/shell 不被 user stop/kill 到
    struct list_head    rq_link;     // 接到 run queue / zombie list
    struct list_head    child_link;  // 接到 parent->children
    struct list_head    children;    // 自己的 child list head
    struct list_head    all_link;    // 接到 all_threads（find_pid 掃這條）
};

// 全 thread 索引；fork/waitpid/stop 透過它找 pid 對應的 thread
extern struct list_head all_threads;

void kthread_init(void);
struct thread* kthread_create(thread_func_t fn);
struct thread* kthread_bootstrap_idle(void);
struct thread* kthread_get_current(void);
int  alloc_pid(void);
void thread_exit(void);

extern void switch_to(struct thread* prev, struct thread* next);

#endif // THREAD_H
