#ifndef PGTABLE_H
#define PGTABLE_H

#define PAGE_OFFSET 0xffffffc000000000UL

#ifndef PAGE_SIZE
#define PAGE_SIZE   (1UL << 12)
#endif
#define PMD_SIZE    (1UL << 21)
#define PGD_SIZE    (1UL << 30)

#define PGD_SHIFT   30
#define PMD_SHIFT   21
#define PTE_SHIFT   12
#define ENTRIES_PER_TABLE 512

#define PTE_V  (1UL << 0)
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_G  (1UL << 5)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)
#define PTE_COW (1UL << 8) /* software bit: was writable, now read-only pending CoW copy */

#define PROT_KERNEL_RWX (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define PROT_KERNEL_RX  (PTE_V | PTE_R | PTE_X        | PTE_G | PTE_A | PTE_D)
#define PROT_KERNEL_RW  (PTE_V | PTE_R | PTE_W        | PTE_G | PTE_A | PTE_D)
#define PROT_MMIO       (PTE_V | PTE_R | PTE_W        | PTE_G | PTE_A | PTE_D)
#define PROT_USER_RX    (PTE_V | PTE_R |          PTE_X | PTE_U | PTE_A | PTE_D)
#define PROT_USER_RW    (PTE_V | PTE_R | PTE_W          | PTE_U | PTE_A | PTE_D)
#define PROT_USER_RWX   (PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D)

#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))
#define PTE_TO_PA(pte)      ((((unsigned long)(pte)) >> 10) << 12)
#define PTE_FLAGS(pte)      ((unsigned long)(pte) & 0x3ffUL)

#define SATP_SV39           (8UL << 60)
#define MAKE_SATP(pgd_pa)   (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))

#ifndef __ASSEMBLER__
static inline unsigned long virt_to_phys(unsigned long va) { return va - PAGE_OFFSET; }
static inline unsigned long phys_to_virt(unsigned long pa) { return pa + PAGE_OFFSET; }

static inline void write_satp_flush(unsigned long pgd_pa) {
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        : : "r"(MAKE_SATP(pgd_pa)) : "memory");
}

static inline void sfence_vma_all(void) {
    asm volatile("sfence.vma zero, zero" ::: "memory");
}
#endif

#endif
