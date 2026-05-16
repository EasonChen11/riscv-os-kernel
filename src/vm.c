#include "vm.h"
#include "pgtable.h"

/*
 * Boot-time page tables. With medany model + higher-half link, `&pgd[0]` is
 * PC-relative: it yields the runtime address (low PA pre-MMU, high VA post-MMU).
 *
 *   pgd            - root page-global directory
 *   pmd_kern[i]    - PMD for the i-th GiB of [ram_pa, ram_pa+4 GiB), shared by
 *                    the identity map and the higher-half linear map
 *   pmd_mmio_hi    - PMD covering the low 1 GiB at high-VA (UART/PLIC/FB)
 */
#define LINEAR_MAP_GIB 4

static unsigned long __attribute__((aligned(4096))) pgd[ENTRIES_PER_TABLE];
static unsigned long __attribute__((aligned(4096))) pmd_kern[LINEAR_MAP_GIB][ENTRIES_PER_TABLE];
static unsigned long __attribute__((aligned(4096))) pmd_mmio_hi[ENTRIES_PER_TABLE];

unsigned long g_kernel_pgd_pa;

void setup_vm(void)
{
    const unsigned long ram_pa = 0x80000000UL;

    /* 1) identity map [ram_pa, ram_pa+4 GiB) using 2 MiB PMD leaves */
    unsigned long id_pgd_idx = (ram_pa >> PGD_SHIFT) & 0x1ff;
    /* 2) high-VA linear map sharing the same PMD leaves */
    unsigned long hh_pgd_idx = ((PAGE_OFFSET + ram_pa) >> PGD_SHIFT) & 0x1ff;

    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        unsigned long pmd_pa = (unsigned long)&pmd_kern[i][0];
        pgd[id_pgd_idx + i] = MAKE_PTE(pmd_pa, PTE_V);
        pgd[hh_pgd_idx + i] = MAKE_PTE(pmd_pa, PTE_V);

        for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
            unsigned long pa = ram_pa + (unsigned long)i * PGD_SIZE + (unsigned long)j * PMD_SIZE;
            pmd_kern[i][j] = MAKE_PTE(pa, PROT_KERNEL_RWX);
        }
    }

    /* 3) MMIO at high VA only: low 1 GiB linearly mapped, no execute */
    unsigned long mmio_pgd_idx = ((PAGE_OFFSET + 0x0UL) >> PGD_SHIFT) & 0x1ff;
    pgd[mmio_pgd_idx] = MAKE_PTE((unsigned long)&pmd_mmio_hi[0], PTE_V);
    for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
        unsigned long pa = (unsigned long)j * PMD_SIZE;
        pmd_mmio_hi[j] = MAKE_PTE(pa, PROT_MMIO);
    }

    g_kernel_pgd_pa = (unsigned long)&pgd[0];

    asm volatile("sfence.vma zero, zero" ::: "memory");
    asm volatile("csrw satp, %0" :: "r"(MAKE_SATP(g_kernel_pgd_pa)) : "memory");
    asm volatile("sfence.vma zero, zero" ::: "memory");
}

void drop_identity_map(void)
{
    const unsigned long ram_pa = 0x80000000UL;
    unsigned long id_pgd_idx = (ram_pa >> PGD_SHIFT) & 0x1ff;
    for (int i = 0; i < LINEAR_MAP_GIB; i++) pgd[id_pgd_idx + i] = 0;

    /* After this point we are running at high VA only; remember the PGD's PA
     * (its address now reads back as high VA, convert to PA for satp value). */
    g_kernel_pgd_pa = (unsigned long)&pgd[0] - PAGE_OFFSET;

    asm volatile("sfence.vma zero, zero" ::: "memory");
}
