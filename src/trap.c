#include "trap.h"
#include "uart.h"
#include "timer.h"

/* Called from handle_exception (assembly) with the saved trap frame. */
// file :file:///Users/eason/Downloads/riscv-spec.html#scause
void do_trap(struct trap_frame *regs) {
    // scause bit 63 = interrupt/exception 區分；低 63 位 = cause code
    long cause_code = regs->scause & SCAUSE_CODE_MASK;
    long is_interrupt = regs->scause & SCAUSE_INTERRUPT_BIT;
    if (is_interrupt) {
        if (cause_code == INT_SUP_TIMER) { // Supervisor timer interrupt
            handle_timer_interrupt();
        } else {
            uart_puts("Unknown interrupt: scause=");
            uart_int(regs->scause);
            uart_putc('\n');
        }
    } else {
        if (cause_code == ENV_CALL_FROM_UMODE) { // scause=8 means ecall from U-mode
            /* ecall from U-mode */
            // uart_puts("[trap] ecall  sepc=0x");
            // uart_hex(regs->sepc);
            uart_puts("=== S-Mode Trap ===\n");
        regs->sepc += 4;  /* skip ecall instruction, resume after it */
        } else {
            uart_puts("scause=");
            uart_int(regs->scause);
            uart_putc('\n');
            uart_puts(" sepc=0x");
            uart_hex(regs->sepc);
            uart_putc('\n');
            uart_puts(" stval=0x");
            uart_int(regs->stval);
            uart_putc('\n');
    }
    }

    
}
