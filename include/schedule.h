#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "thread.h"
#include "list.h"

extern struct list_head zombie_list;   // proc.c (waitpid / stop) 也會 enqueue 進來

// timer-driven preemption 旗標：preempt_tick_cb 標 1，trap exit 前看到就 schedule。
// volatile：跨 trap context 與 thread context，避免編譯器 cache 到暫存器。
extern volatile int need_resched;

void schedule_init(void);
void enqueue_runqueue(struct thread* t);
void enqueue_zombie(struct thread* t);
void kill_zombies(void);
void schedule(void);
void idle(void);

// 啟動 1/32 秒 preemption tick；BE3 之前都靠 cooperative schedule，呼叫這個之後
// 變 preemptive RR。應在 kthread_bootstrap_idle 之後、idle() 之前呼叫。
void start_preempt(void);

#endif // SCHEDULE_H
