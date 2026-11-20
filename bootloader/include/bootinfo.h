#ifndef BOOTINFO_H
#define BOOTINFO_H

extern const void   *g_fdt_base;
extern unsigned long g_mem_base;
extern unsigned long g_mem_size;

void bootinfo_init(const void *fdt);

#endif
