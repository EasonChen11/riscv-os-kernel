#ifndef STARTUP_H
#define STARTUP_H

void  startup_range(void *start, void *end);                    // 已知 heap 範圍時直接初始化
void  startup_init(unsigned long heap_size);              // 掃 reserved list 找第一個空洞並初始化 heap
void *startup_alloc(unsigned long size, unsigned long align);
unsigned long startup_used(void);
unsigned long startup_base(void);

#endif // STARTUP_H
