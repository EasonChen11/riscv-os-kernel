#ifndef THREAD_H
#define THREAD_H

#define KSTACK_SIZE 4096

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
    struct cpu_context  context;   // 必須是第一個成員：switch_to 用 byte offset 直接 sd/ld
    enum thread_state   state;
    int                 pid;
    void               *kstack;
    struct thread      *next;
};

void kthread_init(void);
struct thread* kthread_create(thread_func_t fn);
struct thread* kthread_get_current(void);

extern void switch_to(struct thread* prev, struct thread* next);

#endif // THREAD_H
