#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "thread.h"
#include "list.h"

extern struct list_head zombie_list;   // proc.c (waitpid / stop) 也會 enqueue 進來

void schedule_init(void);
void enqueue_runqueue(struct thread* t);
void enqueue_zombie(struct thread* t);
void kill_zombies(void);
void schedule(void);
void idle(void);

#endif // SCHEDULE_H
