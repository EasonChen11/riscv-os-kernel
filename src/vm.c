#include "vm.h"
#include "pgtable.h"
#include "buddy.h"
#include "string.h"

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

/* per-process PGD: 配一頁、把 kernel half (idx 256..511) 從 kernel PGD copy 過去。
 * 回傳 PA（要塞 satp 用）。 */
unsigned long alloc_pgd(void)
{
    unsigned long *new_pgd = buddy_alloc_pages(0);
    if (!new_pgd) return 0;
    memset(new_pgd, 0, PAGE_SIZE);
    unsigned long *kpgd = (unsigned long *)phys_to_virt(g_kernel_pgd_pa);
    for (int i = 256; i < 512; i++) new_pgd[i] = kpgd[i];
    return virt_to_phys((unsigned long)new_pgd);
}

/* 走某一層表，必要時遞迴釋放下一層；最後把自己 free。
 *
 * level 規則：2 = PGD（只處理 idx 0..255），1 = PMD，0 = PTE leaf。 */
static void free_table(unsigned long table_pa, int level)
{
    unsigned long *t = (unsigned long *)phys_to_virt(table_pa);
    int lo = 0, hi = ENTRIES_PER_TABLE;
    if (level == 2) hi = 256;            /* kernel half 不動 */

    if (level > 0) {
        for (int i = lo; i < hi; i++) {
            unsigned long pte = t[i];
            if (!(pte & PTE_V)) continue;
            if (pte & (PTE_R | PTE_W | PTE_X)) {
                /* leaf at intermediate level (huge page) — lab6 user 不會這樣，略過 */
                continue;
            }
            free_table(PTE_TO_PA(pte), level - 1);
        }
    } else {
        /* level 0: leaf PTE 直接釋放對應 data page */
        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            unsigned long pte = t[i];
            if (!(pte & PTE_V)) continue;
            buddy_free_pages((void *)phys_to_virt(PTE_TO_PA(pte)));
        }
    }
    if (level < 2) buddy_free_pages((void *)t);
}

void free_pgd(unsigned long pgd_pa)
{
    free_table(pgd_pa, 2);
    /* PGD 本身也要 free（free_table 不會 free level=2 自身） */
    buddy_free_pages((void *)phys_to_virt(pgd_pa));
}

static unsigned long *walk_or_alloc(unsigned long *table, unsigned long idx)
{
    if (!(table[idx] & PTE_V)) {
        void *next = buddy_alloc_pages(0);
        memset(next, 0, PAGE_SIZE);
        unsigned long next_pa = virt_to_phys((unsigned long)next);
        table[idx] = MAKE_PTE(next_pa, PTE_V);    /* non-leaf: 只設 V */
        return (unsigned long *)next;
    }
    return (unsigned long *)phys_to_virt(PTE_TO_PA(table[idx]));
}

void map_pages(unsigned long pgd_pa, unsigned long va,
               unsigned long size, unsigned long pa, unsigned long prot)
{
    unsigned long *pgd_va = (unsigned long *)phys_to_virt(pgd_pa);
    for (unsigned long off = 0; off < size; off += PAGE_SIZE) {
        unsigned long v = va + off;
        unsigned long p = pa + off;
        unsigned long vpn2 = (v >> 30) & 0x1ff;
        unsigned long vpn1 = (v >> 21) & 0x1ff;
        unsigned long vpn0 = (v >> 12) & 0x1ff;
        unsigned long *pmd = walk_or_alloc(pgd_va, vpn2);
        unsigned long *pte = walk_or_alloc(pmd, vpn1);
        pte[vpn0] = MAKE_PTE(p, prot);
    }
    sfence_vma_all();
}

/* 走 src 的 user 半邊，逐 leaf PTE 配新 frame + memcpy + map 到 dst。
 * lab6 BE2 deep copy；Adv3 改 CoW。 */
static void copy_user_pte(unsigned long dst_pgd_pa, unsigned long va, unsigned long pte_val)
{
    unsigned long src_pa = PTE_TO_PA(pte_val);
    unsigned long prot   = PTE_FLAGS(pte_val);
    void *dst = buddy_alloc_pages(0);
    memcpy(dst, (void *)phys_to_virt(src_pa), PAGE_SIZE);
    map_pages(dst_pgd_pa, va, PAGE_SIZE, virt_to_phys((unsigned long)dst), prot);
}

static void walk_src_pmd(unsigned long dst_pgd_pa, unsigned long base_va, unsigned long pmd_pa)
{
    unsigned long *pmd = (unsigned long *)phys_to_virt(pmd_pa);
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        unsigned long e = pmd[i];
        if (!(e & PTE_V)) continue;
        if (e & (PTE_R | PTE_W | PTE_X)) continue; /* huge page; lab6 user 不會 */
        unsigned long *pte = (unsigned long *)phys_to_virt(PTE_TO_PA(e));
        for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
            unsigned long pte_val = pte[j];
            if (!(pte_val & PTE_V)) continue;
            unsigned long va = base_va + ((unsigned long)i << 21) + ((unsigned long)j << 12);
            copy_user_pte(dst_pgd_pa, va, pte_val);
        }
    }
}

void vm_copy_user(unsigned long dst_pgd_pa, unsigned long src_pgd_pa)
{
    unsigned long *src_pgd = (unsigned long *)phys_to_virt(src_pgd_pa);
    for (int i = 0; i < 256; i++) {
        unsigned long e = src_pgd[i];
        if (!(e & PTE_V)) continue;
        if (e & (PTE_R | PTE_W | PTE_X)) continue;
        walk_src_pmd(dst_pgd_pa, (unsigned long)i << 30, PTE_TO_PA(e));
    }
}
