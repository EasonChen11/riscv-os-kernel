#include "trap.h"
#include "uart.h"
#include "timer.h"
#include "plic.h"
#include "task.h"
#include "syscall.h"
#include "schedule.h"
#include "thread.h"
#include "signal.h"
#include "mm.h"

// sstatus.SIE (bit 1) 是 S-mode 所有中斷的全域開關；關掉後即使 sie.X=1 也不會觸發
// sstatus.SUM (bit 18) 開了 S-mode 才能存取帶 PTE_U=1 的 user 頁；syscall handler
// 要直接讀 user buffer (uart_write/uart_read) 必須開，否則 load_page_fault tval=user VA
void trap_init(void) {
    asm volatile("csrrs zero, sstatus, %0" :: "r"((1UL << 1) | (1UL << 18)));
}

/* Called from handle_exception (assembly) with the saved trap frame. */
// file :file:///Users/eason/Downloads/riscv-spec.html#scause
void do_trap(struct trap_frame *tf) {
    // scause bit 63 = interrupt/exception 區分；低 63 位 = cause code
    long cause_code = tf->scause & SCAUSE_CODE_MASK;
    long is_interrupt = tf->scause & SCAUSE_INTERRUPT_BIT;
    if (is_interrupt) {
        if (cause_code == INT_SUP_TIMER) { // Supervisor timer interrupt
            handle_timer_interrupt();
        } else if (cause_code == INT_SUP_EXT) { // Supervisor external interrupt
            unsigned int irq = plic_claim();
            if (irq) {
                uart_handle_irq(irq);      // 內部判斷是不是 UART 的 IRQ
                plic_complete(irq);        // 一定要 complete，否則 PLIC 不會再送同號中斷
            }
        } else {
            uart_puts("Unknown interrupt: scause=");
            uart_int(tf->scause);
            uart_putc('\n');
        }
    } else {
        if (cause_code == ENV_CALL_FROM_UMODE) { // scause=8 means ecall from U-mode
            // sepc 指向 ecall 本身；先 +4 再 dispatch，避免 syscall handler 修改 sepc
            // 之後又被無條件覆寫的順序坑（例如 sigreturn 會整盤覆寫 trap_frame）
            tf->sepc += 4;
            do_syscall(tf);
        } else if (cause_code == 12 || cause_code == 13 || cause_code == 15) {
            /* Adv2: 12=inst fetch fault, 13=load fault, 15=store fault */
            do_page_fault(tf, cause_code, tf->stval);
        } else {
            struct thread *cur = kthread_get_current();
            uart_puts("Unknown exception: scause=");
            uart_int(tf->scause);
            uart_puts(" sepc=0x");
            uart_hex(tf->sepc);
            uart_puts(" stval=0x");
            uart_hex(tf->stval);
            uart_puts(" ra=0x");
            uart_hex(tf->ra);
            uart_puts(" pid=");
            uart_int(cur ? cur->pid : -1);
            uart_putc('\n');
        }
    }

    deliver_signal(tf);   // trap exit 前先處理 signal，避免 schedule 切走後才發現有 pending signal 沒 deliver
    // Bottom-half：top-half ISR 透過 add_task 延後的工作在這裡以 SIE=1 執行。
    // nested IRQ 進來會再走一次 do_trap → drain_tasks，自然形成 preemption
    // 與 nested interrupt。trap.S 已存 sstatus/sepc，nested 下兩份 frame 不互相覆寫。
    drain_tasks();

    // BE3 timer-driven preemption：preempt_tick_cb 在 drain_tasks 內標旗，
    // 在這裡（trap 即將出去前）才呼叫 schedule。時機選這裡的理由是 stack 整潔
    // ──drain_tasks 已收尾、sp 在 trap_frame 底部，schedule 切過去再切回來 sp 仍正確。
    if (need_resched) {
        need_resched = 0;
        schedule();
    }
}
