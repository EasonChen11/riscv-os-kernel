#ifndef VM_H
#define VM_H

#include "pgtable.h"

/* boot phase (called from start.S, pre-MMU) */
void setup_vm(void);
void drop_identity_map(void);

/* FDT 解析完後呼叫：把 setup_vm 留下的 2 MiB MMIO blanket 替換成
 * 4 KiB PTE-level 精準映射，只覆蓋 UART/PLIC 真實視窗。 */
void mmio_finalize(unsigned long uart_pa, unsigned long uart_size,
                   unsigned long plic_pa, unsigned long plic_size);

/* buddy_register_range 前呼叫：把 RAM 範圍截到 linear map 蓋得到的長度，
 * 避免 buddy 配出超出 linear map 的 frame。 */
unsigned long vm_clamp_ram_size(unsigned long base, unsigned long size);

/* kernel PGD physical address (for alloc_pgd to clone high-half entries) */
extern unsigned long g_kernel_pgd_pa;

/* per-process page tables */
unsigned long alloc_pgd(void);
void          free_pgd(unsigned long pgd_pa);
void          map_pages(unsigned long pgd_pa, unsigned long va,
                        unsigned long size, unsigned long pa, unsigned long prot);
void          vm_copy_user(unsigned long dst_pgd_pa, unsigned long src_pgd_pa);
void          vm_copy_user_cow(unsigned long dst_pgd_pa, unsigned long src_pgd_pa);
unsigned long vm_lookup_pte(unsigned long pgd_pa, unsigned long va);
int           vm_set_pte_writable_copy(unsigned long pgd_pa, unsigned long va,
                                       unsigned long new_pa, unsigned long prot);

/* context switch helper */
static inline void switch_pgd(unsigned long pgd_pa) {
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        : : "r"(MAKE_SATP(pgd_pa)) : "memory");
}

#endif
