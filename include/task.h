#ifndef TASK_H
#define TASK_H

#include "list.h"

#define PRIO_IDLE         0     // drain loop 最外層的比較基準；user task 不用
#define PRIO_LOW          1     // setTimeout print、demo low task
#define PRIO_NORMAL       5     // UART RX cook（若用到）
#define PRIO_HIGH         9     // 保留給 critical driver / demo high task

typedef void (*task_callback_t)(void*);

struct task {
    struct list_head  link;         // priority queue 節點（依 priority 遞減排序：head=最高）
    task_callback_t   callback;
    void             *arg;
    int               priority;
};

void task_init(void);

// 排入 task。可在 ISR / thread 兩種 context 呼叫；內部自己 irq_save。
// pool 滿或 cb==NULL 時印錯誤後 silently return。
void add_task(task_callback_t cb, void* arg, int priority);

// 從 queue 取優先級比 running_prio 高的 task 依序執行。
// callback 期間 SIE=1（允許 nested IRQ → preemption）。
// 由 do_trap 結尾呼叫；不在其他地方呼叫。
void drain_tasks(void);

#endif /* TASK_H */
