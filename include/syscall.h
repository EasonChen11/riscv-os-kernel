#ifndef SYSCALL_H
#define SYSCALL_H

#include "trap.h"

// syscall 編號：user binary 與 kernel dispatcher 共用同一份。
// 後續 commit 補完 fork/exec/exit 等 handler 時，只在 syscall_table[] 填指標，無須動 enum。
//
// 注意（QEMU virt only）：OpenSBI 在 QEMU 環境下會把部分 a7 值的 U-mode ecall
// 截走當 SBI 處理，不轉給 S-mode；實測規律不一致（a7=2/34/200/202/1002 都被吃，
// 99/100/999 通），所以在 QEMU 上用 user binary 端到端驗證 dispatcher 並不可靠。
// OrangePi RV2 firmware 沒這限制，編號維持 tutorial 規定的 0–7；端到端 demo 等
// 後續 commit (refactor exec) 把 shell 接回後再走 SBI 不衝突的路徑驗證。
enum {
    SYS_GETPID     = 0,
    SYS_UART_READ  = 1,
    SYS_UART_WRITE = 2,
    SYS_EXEC       = 3,
    SYS_FORK       = 4,
    SYS_WAITPID    = 5,
    SYS_EXIT       = 6,
    SYS_STOP       = 7,
    SYS_DISPLAY    = 8,    // a0=BMP*, a1=width, a2=height
    SYS_USLEEP     = 9,    // a0=microseconds；wake-up driven by add_timer_ticks
    NR_SYSCALL,
};

// trap.c ecall from U-mode 分支呼叫；a0 寫回 trap_frame 作為 user-side return value
void do_syscall(struct trap_frame *tf);

#endif /* SYSCALL_H */
