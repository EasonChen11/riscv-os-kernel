#ifndef _VM_H_
#define _VM_H_

#include "pgtable.h"

/* boot phase (called from start.S, pre-MMU) */
void setup_vm(void);
void drop_identity_map(void);

/* kernel PGD physical address (for alloc_pgd to clone high-half entries) */
extern unsigned long g_kernel_pgd_pa;

/* per-process page tables */
unsigned long alloc_pgd(void);
void          free_pgd(unsigned long pgd_pa);
void          map_pages(unsigned long pgd_pa, unsigned long va,
                        unsigned long size, unsigned long pa, unsigned long prot);
void          vm_copy_user(unsigned long dst_pgd_pa, unsigned long src_pgd_pa);

/* context switch helper */
static inline void switch_pgd(unsigned long pgd_pa) {
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        : : "r"(MAKE_SATP(pgd_pa)) : "memory");
}

#endif
