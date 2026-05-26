#include "vm.h"
#include "pgtable.h"
#include "buddy.h"
#include "string.h"

/*
 * Boot-time page tables. With medany model + higher-half link, `&pgd[0]` is
 * PC-relative: it yields the runtime address (low PA pre-MMU, high VA post-MMU).
 *
 *   pgd          - root page-global directory
 *   pmd_kern[i]  - PMD for the i-th GiB of [ram_pa, ram_pa+4 GiB), shared by
 *                  the identity map and the higher-half linear map
 *
 * MMIO 不在這裡處理，由 mmio_finalize() 後續用 4 KiB PTE 接手（lab6.rst:
 * Finer Granularity Paging）。
 */
#define LINEAR_MAP_GIB 2

/* RAM 起點：QEMU virt 從 0x80000000 開始；OrangePi RV2 從 0x0 開始（U-Boot
 * 把 kernel 載到 PA 0x00200000，DRAM 從 0 起算）。設錯 identity map 蓋不到
 * kernel 跑的 PA，satp 寫完一寫就 silent。 */
#ifdef QEMU
#define RAM_PA 0x80000000UL
#else
#define RAM_PA 0x00000000UL
#endif

static unsigned long __attribute__((aligned(4096))) pgd[ENTRIES_PER_TABLE];
static unsigned long __attribute__((aligned(4096))) pmd_kern[LINEAR_MAP_GIB][ENTRIES_PER_TABLE];

unsigned long g_kernel_pgd_pa;

void setup_vm(void)
{
    /* 1) identity map [RAM_PA, RAM_PA+4 GiB) using 2 MiB PMD leaves */
    unsigned long id_pgd_idx = (RAM_PA >> PGD_SHIFT) & 0x1ff;
    /* 2) high-VA linear map sharing the same PMD leaves */
    unsigned long hh_pgd_idx = ((PAGE_OFFSET + RAM_PA) >> PGD_SHIFT) & 0x1ff;

    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        unsigned long pmd_pa = (unsigned long)&pmd_kern[i][0];
        pgd[id_pgd_idx + i] = MAKE_PTE(pmd_pa, PTE_V);
        pgd[hh_pgd_idx + i] = MAKE_PTE(pmd_pa, PTE_V);

        for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
            unsigned long pa = RAM_PA + (unsigned long)i * PGD_SIZE + (unsigned long)j * PMD_SIZE;
            pmd_kern[i][j] = MAKE_PTE(pa, PROT_KERNEL_RWX);
        }
    }

    /* MMIO 留給 mmio_finalize 走 4 KiB PTE 處理；此處只負責 RAM。 */

    g_kernel_pgd_pa = (unsigned long)&pgd[0];

    asm volatile("sfence.vma zero, zero" ::: "memory");
    asm volatile("csrw satp, %0" :: "r"(MAKE_SATP(g_kernel_pgd_pa)) : "memory");
    asm volatile("sfence.vma zero, zero" ::: "memory");
}

void drop_identity_map(void)
{
    unsigned long id_pgd_idx = (RAM_PA >> PGD_SHIFT) & 0x1ff;
    for (int i = 0; i < LINEAR_MAP_GIB; i++) pgd[id_pgd_idx + i] = 0;

    /* 驗算保險：與 setup_vm 內寫入的值一致（同為 pgd 低 PA）。 */
    g_kernel_pgd_pa = (unsigned long)&pgd[0] - PAGE_OFFSET;

    asm volatile("sfence.vma zero, zero" ::: "memory");
}

/* 把 (base, size) 截到 linear map [RAM_PA, RAM_PA + LINEAR_MAP_GIB*1 GiB) 內。
 * buddy 之後配的所有 frame 透過 phys_to_virt 取址，超出 linear map 的尾段會 fault，
 * 故在交給 buddy 前需先 clamp。回傳截過的 size；base 本身不動（假設一定在範圍內）。 */
unsigned long vm_clamp_ram_size(unsigned long base, unsigned long size)
{
    unsigned long linear_end = RAM_PA + (unsigned long)LINEAR_MAP_GIB * (1UL << 30);
    if (base + size > linear_end) {
        return linear_end - base;
    }
    return size;
}

/* 把 [pa, pa+size) 用 4 KiB PTE 映到 high-VA linear 對應位址，PROT_MMIO（含 R/W、無 X）。
 * 透過共用的 map_pages 走 walk_or_alloc，PMD/PTE table 從 buddy 配，故必須在
 * buddy_init 之後呼叫。LINEAR_MAP_GIB=2 下所有 MMIO 區段的 high-VA 都在 linear map
 * 外（pgd[256] / pgd[259] 為 0），walk_or_alloc 不會遇到既有的 2 MiB leaf。 */
void mmio_finalize(unsigned long uart_pa, unsigned long uart_size,
                   unsigned long plic_pa, unsigned long plic_size)
{
    unsigned long uart_sz = (uart_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    unsigned long plic_sz = (plic_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    map_pages(g_kernel_pgd_pa, phys_to_virt(uart_pa), uart_sz, uart_pa, PROT_MMIO);
    map_pages(g_kernel_pgd_pa, phys_to_virt(plic_pa), plic_sz, plic_pa, PROT_MMIO);

#ifdef QEMU
    /* QEMU fw_cfg @ 0x10100000，被 video_init 用來註冊 ramfb；硬編碼，DTB 沒列 reg。
     * 一頁就夠（selector/data port + DMA reg）。 */
    unsigned long fw_pa = 0x10100000UL;
    map_pages(g_kernel_pgd_pa, phys_to_virt(fw_pa), PAGE_SIZE, fw_pa, PROT_MMIO);
#endif
    /* map_pages 結尾自帶 sfence */
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
        unsigned long pmd_ent = pmd[i];
        if (!(pmd_ent & PTE_V)) continue;
        if (pmd_ent & (PTE_R | PTE_W | PTE_X)) continue; /* huge page; lab6 user 不會 */
        unsigned long *pte = (unsigned long *)phys_to_virt(PTE_TO_PA(pmd_ent));
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
        unsigned long pgd_ent = src_pgd[i];
        if (!(pgd_ent & PTE_V)) continue;
        if (pgd_ent & (PTE_R | PTE_W | PTE_X)) continue;
        walk_src_pmd(dst_pgd_pa, (unsigned long)i << 30, PTE_TO_PA(pgd_ent));
    }
}

/* ───────────────────────── Adv3 CoW helpers ───────────────────────── */

/* 在 dst PGD 走相同 VA 路徑，需要時配 PMD/PTE 中介頁；不寫 leaf，回 leaf PTE 槽位址 */
static unsigned long *get_or_alloc_pte_slot(unsigned long pgd_pa, unsigned long va)
{
    unsigned long *pgd_va = (unsigned long *)phys_to_virt(pgd_pa);
    unsigned long vpn2 = (va >> 30) & 0x1ff;
    unsigned long vpn1 = (va >> 21) & 0x1ff;
    unsigned long vpn0 = (va >> 12) & 0x1ff;
    unsigned long *pmd = walk_or_alloc(pgd_va, vpn2);
    unsigned long *pte = walk_or_alloc(pmd, vpn1);
    return &pte[vpn0];
}

/* fork CoW：複製 src 的 leaf PTE 到 dst，**兩邊**都清 W，refcnt++。
 * 後續 store fault 透過 vma 判斷 → CoW copy。 */
static void cow_share_pte(unsigned long dst_pgd_pa, unsigned long va,
                          unsigned long *src_pte_slot)
{
    unsigned long pte_val = *src_pte_slot;
    unsigned long pa      = PTE_TO_PA(pte_val);
    unsigned long flags   = PTE_FLAGS(pte_val);
    /* clear W on both ends；mark soft bit PTE_COW 標示這頁可 CoW upgrade */
    unsigned long ro_flags = (flags & ~PTE_W) | PTE_COW;

    *src_pte_slot = MAKE_PTE(pa, ro_flags);
    unsigned long *dst_pte_slot = get_or_alloc_pte_slot(dst_pgd_pa, va);
    *dst_pte_slot = MAKE_PTE(pa, ro_flags);

    frame_get((void *)phys_to_virt(pa));
}

static void walk_src_pmd_cow(unsigned long dst_pgd_pa, unsigned long base_va,
                             unsigned long pmd_pa)
{
    unsigned long *pmd = (unsigned long *)phys_to_virt(pmd_pa);
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        unsigned long pmd_ent = pmd[i];
        if (!(pmd_ent & PTE_V)) continue;
        if (pmd_ent & (PTE_R | PTE_W | PTE_X)) continue;
        unsigned long *pte = (unsigned long *)phys_to_virt(PTE_TO_PA(pmd_ent));
        for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
            if (!(pte[j] & PTE_V)) continue;
            unsigned long va = base_va + ((unsigned long)i << 21) + ((unsigned long)j << 12);
            cow_share_pte(dst_pgd_pa, va, &pte[j]);
        }
    }
}

void vm_copy_user_cow(unsigned long dst_pgd_pa, unsigned long src_pgd_pa)
{
    unsigned long *src_pgd = (unsigned long *)phys_to_virt(src_pgd_pa);
    for (int i = 0; i < 256; i++) {
        unsigned long pgd_ent = src_pgd[i];
        if (!(pgd_ent & PTE_V)) continue;
        if (pgd_ent & (PTE_R | PTE_W | PTE_X)) continue;
        walk_src_pmd_cow(dst_pgd_pa, (unsigned long)i << 30, PTE_TO_PA(pgd_ent));
    }
    sfence_vma_all();
}

/* 查 va 在 pgd 內的 PTE value；不存在回 0 */
unsigned long vm_lookup_pte(unsigned long pgd_pa, unsigned long va)
{
    unsigned long *pgd_va = (unsigned long *)phys_to_virt(pgd_pa);
    unsigned long vpn2 = (va >> 30) & 0x1ff;
    unsigned long vpn1 = (va >> 21) & 0x1ff;
    unsigned long vpn0 = (va >> 12) & 0x1ff;
    if (!(pgd_va[vpn2] & PTE_V)) return 0;
    unsigned long *pmd = (unsigned long *)phys_to_virt(PTE_TO_PA(pgd_va[vpn2]));
    if (!(pmd[vpn1] & PTE_V)) return 0;
    unsigned long *pte = (unsigned long *)phys_to_virt(PTE_TO_PA(pmd[vpn1]));
    return pte[vpn0];
}

/* CoW store fault：覆寫 va 的 PTE，指向新 PA、加 W、清 PTE_COW。
 * 回 0 成功、-1 失敗（不該發生）。 */
int vm_set_pte_writable_copy(unsigned long pgd_pa, unsigned long va,
                             unsigned long new_pa, unsigned long prot)
{
    unsigned long *pgd_va = (unsigned long *)phys_to_virt(pgd_pa);
    unsigned long vpn2 = (va >> 30) & 0x1ff;
    unsigned long vpn1 = (va >> 21) & 0x1ff;
    unsigned long vpn0 = (va >> 12) & 0x1ff;
    if (!(pgd_va[vpn2] & PTE_V)) return -1;
    unsigned long *pmd = (unsigned long *)phys_to_virt(PTE_TO_PA(pgd_va[vpn2]));
    if (!(pmd[vpn1] & PTE_V)) return -1;
    unsigned long *pte = (unsigned long *)phys_to_virt(PTE_TO_PA(pmd[vpn1]));
    pte[vpn0] = MAKE_PTE(new_pa, prot & ~PTE_COW);
    sfence_vma_all();
    return 0;
}
