#include "mm.h"
#include "thread.h"
#include "dynalloc.h"
#include "buddy.h"
#include "vm.h"
#include "pgtable.h"
#include "string.h"
#include "uart.h"
#include "proc.h"

/* round up to page boundary */
static inline unsigned long round_up_page(unsigned long x) {
    return (x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static inline int regions_overlap(unsigned long a_start, unsigned long a_end,
                                  unsigned long b_start, unsigned long b_end) {
    return a_start < b_end && b_start < a_end;
}

void vma_init_list(struct list_head *head) {
    init_list_head(head);
}

void vma_free_all(struct list_head *head) {
    while (!list_empty(head)) {
        struct vma *v = list_entry(head->next, struct vma, link);
        list_del(&v->link);
        dynalloc_free(v);
    }
}

void vma_copy_list(struct list_head *dst, struct list_head *src) {
    init_list_head(dst);
    struct list_head *p = src->next;
    while (p != src) {
        struct vma *src_v = list_entry(p, struct vma, link);
        struct vma *cp = dynalloc_alloc(sizeof(*cp));
        *cp = *src_v;
        init_list_head(&cp->link);
        list_add_tail(&cp->link, dst);
        p = p->next;
    }
}

/* 找 [base, end) 區段內第一段連續 length 個 page-aligned bytes、不撞任何現有 vma */
static unsigned long vma_find_free(struct thread *me, unsigned long length) {
    unsigned long candidate = MMAP_AREA_BASE;
    while (candidate + length <= MMAP_AREA_END) {
        int conflict = 0;
        struct list_head *p = me->vma_list.next;
        while (p != &me->vma_list) {
            struct vma *v = list_entry(p, struct vma, link);
            if (regions_overlap(candidate, candidate + length, v->start, v->end)) {
                candidate = round_up_page(v->end);
                conflict = 1;
                break;
            }
            p = p->next;
        }
        if (!conflict) return candidate;
    }
    return 0;
}

static int vma_any_overlap(struct thread *me, unsigned long s, unsigned long e) {
    struct list_head *p = me->vma_list.next;
    while (p != &me->vma_list) {
        struct vma *v = list_entry(p, struct vma, link);
        if (regions_overlap(s, e, v->start, v->end)) return 1;
        p = p->next;
    }
    return 0;
}

static void vma_insert_sorted(struct thread *me, struct vma *nv) {
    struct list_head *p = me->vma_list.next;
    while (p != &me->vma_list) {
        struct vma *v = list_entry(p, struct vma, link);
        if (v->start > nv->start) break;
        p = p->next;
    }
    list_add_tail(&nv->link, p);
}

/* mmap(addr, length, prot, flags)
 *   addr  : NULL → kernel 自選；非 NULL 不對齊或撞區 → 改成 hint，自選
 *   prot  : PROT_NONE / READ / WRITE / EXEC 任意或
 *   flags : MAP_ANONYMOUS | MAP_POPULATE
 */
long sys_mmap_impl(struct trap_frame *tf) {
    void         *hint   = (void *)tf->a0;
    unsigned long length = tf->a1;
    int           prot   = (int)tf->a2;
    int           flags  = (int)tf->a3;

    struct thread *me = kthread_get_current();
    if (length == 0) return -1;
    length = round_up_page(length);

    /* 1) 決定起始 VA */
    unsigned long start;
    unsigned long h = (unsigned long)hint;
    if (hint == (void *)0) {
        start = vma_find_free(me, length);
    } else if ((h & (PAGE_SIZE - 1)) || vma_any_overlap(me, h, h + length)) {
        start = vma_find_free(me, length);   /* hint 不合法 → fallback */
    } else {
        start = h;
    }
    if (start == 0) return -1;

    /* 2) prot → PTE flag */
    unsigned long pte_prot = PTE_V | PTE_U | PTE_A | PTE_D;
    if (prot & PROT_READ)  pte_prot |= PTE_R;
    if (prot & PROT_WRITE) pte_prot |= PTE_W;
    if (prot & PROT_EXEC)  pte_prot |= PTE_X;

    /* 3) 建立 vma 並插入 list */
    struct vma *v = dynalloc_alloc(sizeof(*v));
    v->start    = start;
    v->end      = start + length;
    v->pte_prot = pte_prot;
    v->flags    = flags;
    init_list_head(&v->link);
    vma_insert_sorted(me, v);

    /* 4) MAP_POPULATE：立即配 frame + map。否則留給 demand paging (Adv2) */
    if (flags & MAP_POPULATE) {
        for (unsigned long va = start; va < start + length; va += PAGE_SIZE) {
            void *p = buddy_alloc_pages(0);
            memset(p, 0, PAGE_SIZE);
            map_pages(me->pgd_pa, va, PAGE_SIZE,
                      virt_to_phys((unsigned long)p), pte_prot);
        }
    }
    return (long)start;
}

/* Adv2: 查 va 落在哪個 vma；找不到回 NULL */
static struct vma *vma_lookup(struct thread *me, unsigned long va) {
    struct list_head *p = me->vma_list.next;
    while (p != &me->vma_list) {
        struct vma *v = list_entry(p, struct vma, link);
        if (va >= v->start && va < v->end) return v;
        p = p->next;
    }
    return (void *)0;
}

/* fault cause 12 = inst fetch, 13 = load, 15 = store/AMO */
void do_page_fault(struct trap_frame *tf, unsigned long cause, unsigned long bad_va) {
    struct thread *me = kthread_get_current();
    struct vma *v = vma_lookup(me, bad_va);

    if (!v) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }

    /* 落在合法 vma 內。檢查 prot 是否允許此操作；不允許 → CoW (Adv3 處理) 或 segfault */
    int needs_w = (cause == 15);
    int needs_x = (cause == 12);
    if (needs_w && !(v->pte_prot & PTE_W)) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }
    if (needs_x && !(v->pte_prot & PTE_X)) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }

    /* demand paging：配一頁 + zero + map。rst 要求格式 "[Translation fault]: %lx" */
    uart_puts("[Translation fault]: ");
    uart_hex(bad_va);
    uart_puts("\n");
    void *p = buddy_alloc_pages(0);
    memset(p, 0, PAGE_SIZE);
    unsigned long va_aligned = bad_va & ~(PAGE_SIZE - 1);
    map_pages(me->pgd_pa, va_aligned, PAGE_SIZE,
              virt_to_phys((unsigned long)p), v->pte_prot);
    /* sret 後 user 重試該指令 */
}
