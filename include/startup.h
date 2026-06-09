#ifndef STARTUP_H
#define STARTUP_H

void  startup_init(unsigned long heap_size);
void *startup_alloc(unsigned long size, unsigned long align);

#endif // STARTUP_H
