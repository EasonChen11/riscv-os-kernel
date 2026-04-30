#ifndef SIGNAL_H
#define SIGNAL_H

#include "trap.h"

#define NSIG 32
#define SIGTERM 15

void deliver_signal(struct trap_frame *tf);
long do_signal(int signum, unsigned long handler);
long do_kill(int pid, int signum);
void do_sigreturn(struct trap_frame *tf);

#endif // SIGNAL_H
