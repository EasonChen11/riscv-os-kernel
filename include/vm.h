#ifndef _VM_H_
#define _VM_H_

#include "pgtable.h"

/* boot phase (called from start.S, pre-MMU) */
void setup_vm(void);
void drop_identity_map(void);

/* kernel PGD physical address (for alloc_pgd to clone high-half entries) */
extern unsigned long g_kernel_pgd_pa;

#endif
