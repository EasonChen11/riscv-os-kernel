#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "thread.h"

void schedule_init(void);
void enqueue_runqueue(struct thread* t);
void enqueue_zombie(struct thread* t);
void kill_zombies(void);
void schedule(void);
void idle(void);

#endif // SCHEDULE_H
