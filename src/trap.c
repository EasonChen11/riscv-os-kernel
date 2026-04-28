#include "trap.h"
#include "uart.h"
#include "timer.h"
#include "plic.h"
#include "task.h"
#include "syscall.h"

// sstatus.SIE (bit 1) 是 S-mode 所有中斷的全域開關；關掉後即使 sie.X=1 也不會觸發
void trap_init(void) {
    asm volatile("csrrs zero, sstatus, %0" :: "r"(1 << 1));
}

/* Called from handle_exception (assembly) with the saved trap frame. */
// file :file:///Users/eason/Downloads/riscv-spec.html#scause
void do_trap(struct trap_frame *regs) {
    // scause bit 63 = interrupt/exception 區分；低 63 位 = cause code
    long cause_code = regs->scause & SCAUSE_CODE_MASK;
    long is_interrupt = regs->scause & SCAUSE_INTERRUPT_BIT;
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
            uart_int(regs->scause);
            uart_putc('\n');
        }
    } else {
        if (cause_code == ENV_CALL_FROM_UMODE) { // scause=8 means ecall from U-mode
            // sepc 指向 ecall 本身；先 +4 再 dispatch，避免 syscall handler 修改 sepc
            // 之後又被無條件覆寫的順序坑（例如 sigreturn 會整盤覆寫 trap_frame）
            regs->sepc += 4;
            do_syscall(regs);
        } else {
            uart_puts("Unknown exception: scause=");
            uart_int(regs->scause);
            uart_putc('\n');
        }
    }

    // Bottom-half：top-half ISR 透過 add_task 延後的工作在這裡以 SIE=1 執行。
    // nested IRQ 進來會再走一次 do_trap → drain_tasks，自然形成 preemption
    // 與 nested interrupt。trap.S 已存 sstatus/sepc，nested 下兩份 frame 不互相覆寫。
    drain_tasks();
}
