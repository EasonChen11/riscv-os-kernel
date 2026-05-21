#include "timer.h"
#include "sbi.h"
#include "uart.h"
#include "fdt.h"
#include "bootinfo.h"
#include "dynalloc.h"
#include "task.h"
#include "irq.h"

// setTimeout 的訊息 + 下命令時戳獨立 alloc，讓 timer_event 到期後可立刻 pool_free，
// 下游 task 從 ctx 讀資料自行負責 dynalloc_free(ctx)。
struct setTimeout_ctx {
    unsigned long cmd_tick;
    char          msg[TIMER_MSG_LEN];
};

static unsigned long boot_time = 0;
static unsigned long CPU_FREQ = 10000000;


// ─── 動態 pool（size 由 timer_init 決定，storage 從 dynalloc 取） ──────
static struct timer_event *pool;
static unsigned long       pool_size;

// 從靜態池取一個空閒 slot；池滿回 NULL。
// 不自己 irq_save：唯一 caller 是 add_timer_ticks，已在外圍包好。
static struct timer_event* pool_alloc(void) {
    for (unsigned long i = 0; i < pool_size; i++) {
        if (!pool[i].in_use) {
            pool[i].in_use = 1;
            init_list_head(&pool[i].link);
            return &pool[i];
        }
    }
    return (void*)0;
}

// 釋放 slot：呼叫端應先 list_del 拆下 queue。
// 不自己 irq_save：唯一 caller 是 handle_timer_interrupt（trap 內 SIE=0）。
static void pool_free(struct timer_event* e) {
    e->in_use = 0;
}

// ─── Timer queue（sorted by expire_tick，遞增） ─────────────────────────
static struct list_head timer_queue;

// 把硬體 timer 對準 queue head。queue 空就推到極遠避免誤觸發。
// 呼叫端需已持有 SIE=0（irq_save 中）。
static inline void reprogram_hw(void) {
    if (list_empty(&timer_queue)) {
        sbi_set_timer(~0UL);
    } else {
        struct timer_event* head =
            list_entry(timer_queue.next, struct timer_event, link);
        sbi_set_timer(head->expire_tick);
    }
}

// ─── Public API ─────────────────────────────────────────────────────────

// 把已填好欄位的 event 按 expire_tick 插進 queue 並重排硬體 timer。
// 不自己 irq_save：唯一 caller 是 add_timer_ticks，已在外圍包好。
static void timer_queue_insert(struct timer_event* e) {
    // 走 queue 找第一個 expire_tick 比新 event 大的節點；若整圈沒找到，cur 會停在 &timer_queue。
    struct list_head* cur;
    for (cur = timer_queue.next; cur != &timer_queue; cur = cur->next) {
        struct timer_event* ev = list_entry(cur, struct timer_event, link);
        if (ev->expire_tick > e->expire_tick) break;
    }
    // list_add(new, prev) 把 new 插到 prev 之後；cur->prev 即「比我小的最後一個」。
    // queue 空時 cur==&timer_queue、cur->prev==&timer_queue，照樣正確。
    list_add(&e->link, cur->prev);
    reprogram_hw();
}

// 註冊 one-shot timer：ticks 個 mtime tick 後排入 task queue 呼叫 cb(arg)。
// 可在 ISR 或 thread 上下文呼叫。preempt / usleep 等 sub-second 場景直接用這個。
// 一個 irq_save 蓋全程：pool_alloc + 寫 event 欄位 + queue insert + reprogram timer
// 全部 atomic。pool_alloc / timer_queue_insert 為內部靜態 helper、不再自包，避免冗餘。
void add_timer_ticks(timer_callback_t cb, void* arg, unsigned long ticks) {
    if (ticks == 0) {
        uart_puts("add_timer_ticks: invalid ticks\n");
        return;
    }
    unsigned long s = irq_save();

    unsigned long now;
    asm volatile("rdtime %0" : "=r"(now));

    struct timer_event* e = pool_alloc();
    if (!e) {
        irq_restore(s);
        uart_puts("add_timer_ticks: pool full\n");
        return;
    }
    e->callback    = cb;
    e->arg         = arg;
    e->expire_tick = now + ticks;

    timer_queue_insert(e);

    irq_restore(s);
}

// 秒級包裝：把 sec 換算成 ticks 後委派給 add_timer_ticks。
void add_timer(timer_callback_t cb, void* arg, int sec) {
    if (sec <= 0) {
        uart_puts("add_timer: invalid sec\n");
        return;
    }
    add_timer_ticks(cb, arg, (unsigned long)sec * CPU_FREQ);
}

unsigned long timer_get_freq(void) {
    return CPU_FREQ;
}

// 執行在 bottom-half（task context, SIE=1）。arg 指向 shell 替它 alloc 的 ctx。
static void setTimeout_cb(void* arg) {
    struct setTimeout_ctx* ctx = (struct setTimeout_ctx*)arg;
    unsigned long now;
    asm volatile("rdtime %0" : "=r"(now));
    unsigned long tpms = CPU_FREQ / 1000;
    uart_puts("[setTimeout cmd=");
    uart_int((ctx->cmd_tick - boot_time) / tpms);
    uart_puts("ms fired=");
    uart_int((now - boot_time) / tpms);
    uart_puts("ms diff=");
    uart_int((now - ctx->cmd_tick) / tpms);
    uart_puts("ms] ");
    uart_puts(ctx->msg);
    uart_putc('\n');
    dynalloc_free(ctx);
}

void setTimeout(int sec, const char* msg) {
    if (sec <= 0 || sec > 10000) {
        uart_puts("setTimeout: sec must be 1..10000\n");
        return;
    }
    unsigned long now;
    asm volatile("rdtime %0" : "=r"(now));

    struct setTimeout_ctx* ctx = (struct setTimeout_ctx*)dynalloc_alloc(sizeof(struct setTimeout_ctx));
    if (!ctx) {
        uart_puts("setTimeout: alloc failed\n");
        return;
    }
    ctx->cmd_tick = now;
    unsigned long i = 0;
    for (; msg[i] && i < TIMER_MSG_LEN - 1; i++) ctx->msg[i] = msg[i];
    ctx->msg[i] = '\0';

    add_timer(setTimeout_cb, ctx, sec);

    uart_puts("setTimeout: scheduled ");
    uart_int(sec);
    uart_puts("s -> \"");
    uart_puts(ctx->msg);
    uart_puts("\"\n");
}


// 由 trap 在 scause=STI 時呼叫。硬體進 trap 已關 SIE，這裡不需再 irq_save。
// 所有 callback 統一延後到 task context (SIE=1)，ISR 本身只做 pop + enqueue + free event。
void handle_timer_interrupt(void) {
    unsigned long now;
    asm volatile("rdtime %0" : "=r"(now));

    while (!list_empty(&timer_queue)) {
        struct timer_event* head =
            list_entry(timer_queue.next, struct timer_event, link);
        if (head->expire_tick > now) break;

        list_del(&head->link);
        if (head->callback)
            add_task(head->callback, head->arg, PRIO_LOW);
        pool_free(head);
    }
    reprogram_hw();
}

void timer_init(void) {
    CPU_FREQ = fdt_get_cpu_freq(g_fdt_base);
    asm volatile("rdtime %0" : "=r"(boot_time));

    init_list_head(&timer_queue);

    pool_size = TIMER_MAX_EVENTS;
    pool = (struct timer_event*)dynalloc_alloc(
        sizeof(struct timer_event) * pool_size);
    for (unsigned long i = 0; i < pool_size; i++)
        pool[i].in_use = 0;

    asm volatile("csrrs zero, sie, %0" :: "r"(1UL << 5));   // sie.STIE 開啟
    drain_tasks();
}
