#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "thread.h"

void schedule_init(void);
void enqueue_runqueue(struct thread* t);
void schedule(void);
void idle(void);

#endif // SCHEDULE_H
