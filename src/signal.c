#include "signal.h"
#include "thread.h"
#include "proc.h"     // do_exit / find_pid
#include "uart.h"
#include "buddy.h"    // signal stack 配/釋放走 buddy（一頁就夠）
#include "string.h"   // memcpy 拷 trampoline

// sigreturn trampoline：兩條 RISC-V instruction，照 little-endian byte 排
//   li a7, 11   ; addi a7, x0, 11  → 0x00B00893
//   ecall       ; 0x00000073
// 放在 .rodata；deliver_signal 把這 8 byte 複製進 user signal stack 頂端，
// 設 user ra=trampoline 後 handler 結束 ret 進去，下一條 ecall 觸發 SYS_SIGRETURN
// 把 trap_frame 復原 + 釋放 signal stack。
const unsigned char sigreturn_trampoline[8] = {
    0x93, 0x08, 0xb0, 0x00,   // li a7, 11
    0x73, 0x00, 0x00, 0x00,   // ecall
};

// 由 do_trap 在 trap exit 前呼叫；user thread 才會走完整路徑。
//
// (!me) 的早退守衛是必要的：do_trap 在 boot 早期 trap 路徑可能進來
// （例如 IRQ-mode UART 開啟後 RX 殘留 byte 觸發 trap），此時 tp 還沒
// 被 kthread_bootstrap_idle 設過、是 0 / 垃圾值。若沒這個 guard，後續
// me->signal_pending 會去 deref address 0+offset 引發 load fault →
// nested trap → infinite trap loop。
void deliver_signal(struct trap_frame *tf) {
    struct thread *me = kthread_get_current();
    if (!me) return;
    if (me->is_kernel) return;
    if (me->signal_pending == 0) return;
    if (me->signal_handling) return;   // 正在跑 user handler，不要 nested

    // 一次只 deliver 一個 signal：找最低 set bit、清掉 pending、處理。
    // 其他還沒處理的 pending bit 留到下次 trap exit 再進來。
    for (int s = 0; s < NSIG; s++) {
        if (!(me->signal_pending & (1u << s))) continue;
        me->signal_pending &= ~(1u << s);

        unsigned long h = me->signal_handlers[s];
        if (h == 0) {
            // default action：lab spec 只規定 SIGTERM 預設要 terminate；
            // 其他 signal 在這個 lab 不測，silently consume。
            if (s == SIGTERM) {
                uart_puts("[signal] default SIGTERM -> do_exit\n");
                do_exit(0);     // not return
            }
            return;
        }

        // user handler path：
        // 1. 完整存 tf 一份，sigreturn 整盤覆寫回去（包含 sepc/sstatus）
        // 2. 配一頁 user stack；trampoline 8 byte 放在 stack 上端、16-byte align
        // 3. 改 tf：sepc=handler、sp=trampoline、ra=trampoline
        //    handler 跑完 ret 進 trampoline → ecall 觸發 SYS_SIGRETURN
        me->signal_handling   = true;
        me->signal_saved_tf   = *tf;
        me->signal_stack      = buddy_alloc_pages(0);

        unsigned long top   = (unsigned long)me->signal_stack + PAGE_SIZE;
        unsigned long tramp = (top - sizeof(sigreturn_trampoline)) & ~0xfUL;
        memcpy((void *)tramp, sigreturn_trampoline, sizeof(sigreturn_trampoline));
        // 寫完指令必須 fence.i：CPU I-cache 可能還是舊內容（這頁是新配的），
        // sret 進 user 後 fetch trampoline byte 可能撈到 stale cache line。
        asm volatile("fence.i" ::: "memory");

        tf->sepc = h;
        tf->sp   = tramp;
        tf->ra   = tramp;
        return;
    }
}

// SYS_SIGNAL：註冊 user handler；signum 0 表示恢復 default。
// handler 已是 user image 內的 PC-relative 位址（user binary 是 PIE）。
long do_signal(int signum, unsigned long handler) {
    if (signum < 0 || signum >= NSIG) return -1;
    struct thread *me = kthread_get_current();
    me->signal_handlers[signum] = handler;
    return 0;
}

// SYS_KILL：對 pid 標 signal pending；下一次該 thread trap exit 時 deliver。
// 不真正切走目前 thread；目標即使在 WAITING 也只是設 bit，等它自己醒來才處理。
long do_kill(int pid, int signum) {
    if (signum < 0 || signum >= NSIG) return -1;
    if (pid <= 0) return -1;            // 0 是 idle，禁止
    struct thread *t = find_pid(pid);
    if (!t) return -1;
    if (t->is_kernel) return -1;        // 擋 idle / kernel-side shell
    t->signal_pending |= (1u << signum);
    return 0;
}

// SYS_SIGRETURN：trampoline 觸發；把 trap_frame 復原成 deliver_signal 當下的快照。
// caller (syscall.c dispatcher) 不應再寫 tf->a0 = ret，因為 *tf 已被整盤覆寫
// （a0 當然包括），需要在 dispatcher 用 return 跳過 a0 寫回。
void do_sigreturn(struct trap_frame *tf) {
    struct thread *me = kthread_get_current();
    if (!me || !me->signal_handling) return;
    if (me->signal_stack) {
        buddy_free_pages(me->signal_stack);
        me->signal_stack = (void *)0;
    }
    *tf = me->signal_saved_tf;
    me->signal_handling = false;
    uart_puts("[signal] sigreturn restored\n");
}
