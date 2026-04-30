#ifndef SIGNAL_H
#define SIGNAL_H

#include "trap.h"

#define NSIG 32
#define SIGTERM 15

// trampoline 是兩條 RISC-V instruction (li a7,11 + ecall) 的 byte 陣列；
// signal handler 跑完 ret 過去，觸發 SYS_SIGRETURN 回 kernel。
// 因為 trampoline 必須 user-mode 可執行，deliver_signal 把它複製到一頁
// freshly-allocated user signal stack 後設 ra → trampoline_addr。
extern const unsigned char sigreturn_trampoline[8];

void deliver_signal(struct trap_frame *tf);
long do_signal(int signum, unsigned long handler);
long do_kill(int pid, int signum);
void do_sigreturn(struct trap_frame *tf);

#endif // SIGNAL_H
