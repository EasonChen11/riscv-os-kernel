#include "task.h"
#include "uart.h"
#include "dynalloc.h"

// ─── IRQ save/restore：保護 queue / running_prio 與 dynalloc 呼叫 ────────
static inline unsigned long irq_save(void) {
    unsigned long s;
    asm volatile("csrrc %0, sstatus, %1" : "=r"(s) : "i"(1 << 1));
    return s;
}
static inline void irq_restore(unsigned long s) {
    asm volatile("csrs sstatus, %0" :: "r"(s & (1 << 1)));
}

// 手動開 / 關 SIE：drain 跑 callback 時需要 SIE=1 以接受 nested IRQ
static inline void sie_enable(void)  { asm volatile("csrs sstatus, %0" :: "r"(1UL << 1)); }
static inline void sie_disable(void) { asm volatile("csrc sstatus, %0" :: "r"(1UL << 1)); }

// BSS 零初值 — 必須由 task_init() 在開任何中斷前執行 init_list_head 設成自指循環。
// 不能用靜態 self-referencing 初值（`{ &task_queue, &task_queue }`）：
// start.S 做 kernel 自我搬遷（0x80200000 → 0x82000000）時只做 raw copy，
// .data 裡的絕對位址仍指向舊的 linked 位置，搬遷後 self-pointing 變成 dangling pointer。
static struct list_head task_queue;

// 代表「目前正在 drain 哪個 priority 的 task」。
// 初始 PRIO_IDLE 使第一層 drain 能 pop 任何 priority>=1 的 task；
// 進入 callback 前設成該 task 的 priority；
// callback 返回後 restore，讓外層 drain 恢復原本比較基準。
static volatile int running_prio = PRIO_IDLE;

// ─── Public API ────────────────────────────────────────────────────────────

// 每次呼叫都 dynalloc_alloc 一個 slot。caller 包含 ISR 與 thread 兩種 context，
// irq_save 同時保護 queue 與 dynalloc 的內部結構（dynalloc 本身未 ISR-safe）。
void add_task(task_callback_t cb, void* arg, int priority) {
    if (!cb) {
        uart_puts("add_task: cb is NULL\n");
        return;
    }
    unsigned long s = irq_save();

    struct task* t = (struct task*)dynalloc_alloc(sizeof(struct task));
    if (!t) {
        irq_restore(s);
        uart_puts("add_task: alloc failed\n");
        return;
    }
    init_list_head(&t->link);
    t->callback = cb;
    t->arg      = arg;
    t->priority = priority;

    // priority 遞減排序：head 最高。同 priority 用 FIFO（走到第一個嚴格更小的節點插在其 prev）
    struct list_head* cur;
    for (cur = task_queue.next; cur != &task_queue; cur = cur->next) {
        struct task* ev = list_entry(cur, struct task, link);
        if (ev->priority < t->priority) break;
    }
    list_add(&t->link, cur->prev);

    irq_restore(s);
}

// 由 do_trap 結尾呼叫（SIE=0 進來）。亦可在 thread context 直接呼叫（shell demo），
// 進出只改本地 SIE 狀態，不影響呼叫者。
// 遞迴層數 = 系統中活躍 task 的 priority 級數，stack 消耗有上限。
// 由 do_trap 結尾呼叫（SIE=0 進來）。亦可在 thread context 直接呼叫（shell demo），
// 進出只改本地 SIE 狀態，不影響呼叫者。
// 遞迴層數 = 系統中活躍 task 的 priority 級數，stack 消耗有上限。
void drain_tasks(void) {
    // 存下呼叫者的 SIE，結束時 restore。drain loop 內部嚴格控制 SIE：
    //  - queue / running_prio 的 RMW 在 SIE=0 下做
    //  - callback 在 SIE=1 下跑以允許 nested IRQ → preemption
    unsigned long caller_state = irq_save();

    while (1) {
        if (list_empty(&task_queue)) break;
        struct task* head = list_entry(task_queue.next, struct task, link);

        // 只跑嚴格比當前 running_prio 大的 task；
        // 同 prio 排隊等外層 drain 完成（避免 starvation 與無限遞迴）。
        if (head->priority <= running_prio) break;

        list_del(&head->link);
        int saved_prio = running_prio;
        running_prio = head->priority;      // nested drain 進來會看到這個新基準

        task_callback_t cb = head->callback;
        void* arg = head->arg;

        sie_enable();                       // SIE=1：允許 nested IRQ
        cb(arg);
        sie_disable();                      // SIE=0：保護後續 RMW

        running_prio = saved_prio;          // 先 restore running_prio，再 free slot
        dynalloc_free(head);
    }

    irq_restore(caller_state);
}

void task_init(void) {
    init_list_head(&task_queue);
    running_prio = PRIO_IDLE;
}
