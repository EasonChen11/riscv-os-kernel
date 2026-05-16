#include "schedule.h"
#include "buddy.h"
#include "dynalloc.h"
#include "uart.h"
#include "timer.h"
#include "task.h"
#include "irq.h"
#include "vm.h"
#include "mm.h"

static struct list_head runqueue;
struct list_head zombie_list;          // proc.c 也會 enqueue（waitpid reap、stop）

volatile int need_resched = 0;
#define PREEMPT_HZ 32                  // 1/32 秒一次 schedule check

void schedule_init(void) {
    init_list_head(&runqueue);
    init_list_head(&zombie_list);
}

void enqueue_runqueue(struct thread* t) {
    list_add_tail(&t->rq_link, &runqueue);
}

// caller 須先 list_del 並設 state = ZOMBIE
void enqueue_zombie(struct thread* t) {
    list_add_tail(&t->rq_link, &zombie_list);
}

// idle 呼叫；irq_save 防 find_pid 在 list_del 中間被打斷踩 stale pointer
void kill_zombies(void) {
    unsigned long flags = irq_save();
    while (!list_empty(&zombie_list)) {
        struct thread* z = list_entry(zombie_list.next, struct thread, rq_link);
        list_del(&z->rq_link);
        list_del(&z->all_link);          // 從 all_threads 摘下，find_pid 不再回它
        list_del_init(&z->child_link);   // 從 parent.children 拔下（含 idle），防 dangling
        uart_puts("[zombie freed pid=");
        uart_hex(z->pid);
        uart_puts("]\n");
        buddy_free_pages(z->kstack);
        if (z->ustack) buddy_free_pages(z->ustack);
        if (z->pgd_pa) free_pgd(z->pgd_pa);   /* lab6: 釋放 user PGD + 走訪所有 leaf page */
        vma_free_all(&z->vma_list);
        dynalloc_free(z);
    }
    irq_restore(flags);
}

// 整段 irq_save：runqueue RMW + switch_to 暫存器存讀中間若被 preempt timer
// 打斷會遞迴 schedule，partial context save/load 會炸
void schedule(void) {
    unsigned long flags = irq_save();

    struct thread* prev = kthread_get_current();
    if (!prev) goto out;

    if (prev->state == THREAD_RUNNING) {
        list_del(&prev->rq_link);
        list_add_tail(&prev->rq_link, &runqueue);
    }
    if (list_empty(&runqueue)) goto out;

    struct thread* next = list_entry(runqueue.next, struct thread, rq_link);
    if (next == prev) goto out;

    /* satp 切換必須在 switch_to **之前**：對首次跑的 thread（fork child / 新
     * kthread），switch_to 的 ret 直接跳到 next->context.ra，不會回到這個函式，
     * 後面的程式碼跑不到。 */
    switch_pgd(next->pgd_pa ? next->pgd_pa : g_kernel_pgd_pa);
    switch_to(prev, next);
out:
    irq_restore(flags);
}

void idle(void) {
    while (1) {
        kill_zombies();   // idle 是唯一保證會被排到的 thread，當 reaper 不會餓到
        schedule();
    }
}

// 不直接 schedule：cb 切走會打亂 drain_tasks 結束點；統一交 trap exit 處理
static void preempt_tick_cb(void* arg) {
    (void)arg;
    add_timer_ticks(preempt_tick_cb, (void*)0, timer_get_freq() / PREEMPT_HZ);
    need_resched = 1;
}

void start_preempt(void) {
    add_timer_ticks(preempt_tick_cb, (void*)0, timer_get_freq() / PREEMPT_HZ);
}
