#include"signal.h"
#include "thread.h"
#include "proc.h" // do_exit
#include "uart.h" // uart_puts

// 由 do_trap 在 trap exit 前呼叫；user thread 才會走完整路徑。
//
// (!me) 的早退守衛是必要的：do_trap 在 boot 早期 trap 路徑可能進來
// （例如 IRQ-mode UART 開啟後 RX 殘留 byte 觸發 trap），此時 tp 還沒
// 被 kthread_bootstrap_idle 設過、是 0 / 垃圾值。若沒這個 guard，後續
// me->signal_pending 會去 deref address 0+offset 引發 load fault →
// nested trap → infinite trap loop。
void deliver_signal(struct trap_frame *tf) {
    (void)tf;
    struct thread *me = kthread_get_current();
    if (!me) return;
    if (me->signal_pending == 0 || me->is_kernel) return;

    // 掃 bitmap 找第一個 set bit 處理。default action 對 SIGTERM 會
    // 走 do_exit 不返回；其他 signal 在 commit #1 暫不處理（POSIX
    // 預設多數是 terminate，但本 lab 規格只測 SIGTERM）。
    for (int s = 0; s < NSIG; s++) {
        if (!(me->signal_pending & (1u << s))) continue;
        // 先清 bit 再執行：萬一 do_exit 沒一次殺乾淨被切回來，不會
        // 同個 signal 再觸發第二次。
        me->signal_pending &= ~(1u << s);

        // commit #2 會在這裡接 user handler 路徑（signal_handlers[s] != 0）
        // 目前所有 signal 走 default。
        if (s == SIGTERM) {
            uart_puts("[signal] default SIGTERM -> do_exit\n");
            do_exit(0);
        }
    }
}