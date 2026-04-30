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
    struct cpu_context  context;     // 必須是第一個成員：switch_to 用 byte offset 直接 sd/ld
    enum thread_state   state;       // RUNNING：在 run queue；WAITING：不在 run queue；ZOMBIE：已 exit、等 parent reap
    int                 pid;         // thread id；kernel thread 由 kthread_create 分配，user thread fork 後繼承 parent pid
    int                 ppid;        // parent pid: fork 設定；root thread 為 0
    void               *kstack;      // kernel stack；每 thread 固定一頁，切到這 thread 時 sp 就指向這頁的頂端
    void               *ustack;      // U-mode stack；純 kernel thread 為 NULL
    int                 wait_pid;    // waitpid 等的 child pid；-1 表示任意
    bool                is_kernel;   // true=kernel thread；保護 idle/shell 不被 user stop/kill 到
    // list link
    struct list_head    rq_link;     // 接到 run queue / zombie list
    struct list_head    child_link;  // 接到 parent->children
    struct list_head    children;    // 自己的 child list head
    struct list_head    all_link;    // 接到 all_threads（find_pid 掃這條）
    // signal handler
    unsigned int signal_pending;            // bit i: signal i pending
    unsigned long signal_handlers[NSIG];    // 0=default，其他=user handler 入口 addr
    // signal delivery 期間用的暫存：deliver_signal 配 stack + 存 tf，sigreturn 還原並 free
    struct trap_frame   signal_saved_tf;    // 跳 user handler 前完整存一份原 tf；sigreturn 整盤覆寫回去
    void               *signal_stack;       // signal handler 專用一頁 user stack；sigreturn 釋放
    bool                signal_handling;    // 防 nested delivery（lab spec 不要求 nested）
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
