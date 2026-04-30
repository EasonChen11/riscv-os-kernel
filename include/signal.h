#ifndef SIGNAL_H
#define SIGNAL_H

#include "trap.h"

#define NSIG 32
#define SIGTERM 15

void deliver_signal(struct trap_frame *tf);

#endif // SIGNAL_H