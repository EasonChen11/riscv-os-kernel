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
            if (!p) {
                list_del(&v->link);
                dynalloc_free(v);
                return -1;
            }
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

/* user stack 動態擴張上限：總長度不超過 8 MB（仿 Linux 預設 RLIMIT_STACK）；
 * 同時 fault va 須緊鄰目前 vma 下緣 1 MB 內，避免 wild pointer 被誤判成 stack-grow。 */
#define STACK_RLIMIT          (8UL  * 1024 * 1024)
#define STACK_GROWSDOWN_GAP   (1UL  * 1024 * 1024)

/* 嘗試把 me->stack_vma 往下擴到包含 bad_va。成功回傳 stack vma，失敗 NULL。
 * 條件：(a) 有 stack_vma 且 va 落在它下方；(b) 與目前 vma 下緣的距離未超過 GAP；
 * (c) 擴張後總長度未超過 RLIMIT。 */
static struct vma *try_expand_stack(struct thread *me, unsigned long bad_va) {
    struct vma *sv = me->stack_vma;
    if (!sv) return (void *)0;
    if (bad_va >= sv->start) return (void *)0;
    unsigned long gap = sv->start - bad_va;
    if (gap > STACK_GROWSDOWN_GAP) return (void *)0;
    unsigned long new_start = bad_va & ~(PAGE_SIZE - 1);
    if (sv->end - new_start > STACK_RLIMIT) return (void *)0;
    sv->start = new_start;
    return sv;
}

/* fault cause 12 = inst fetch, 13 = load, 15 = store/AMO */
void do_page_fault(struct trap_frame *tf, unsigned long cause, unsigned long bad_va) {
    struct thread *me = kthread_get_current();
    struct vma *v = vma_lookup(me, bad_va);

    if (!v) {
        v = try_expand_stack(me, bad_va);
    }
    if (!v) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }

    int needs_w = (cause == 15);
    int needs_x = (cause == 12);
    /* exec faulting in NX region 直接 segfault */
    if (needs_x && !(v->pte_prot & PTE_X)) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }

    unsigned long va_aligned = bad_va & ~(PAGE_SIZE - 1);
    unsigned long cur_pte = vm_lookup_pte(me->pgd_pa, bad_va);

    /* (1) PTE 存在 + store fault + PTE 沒 W → CoW (Adv3) 或寫只讀 vma */
    if ((cur_pte & PTE_V) && needs_w && !(cur_pte & PTE_W)) {
        if (!(v->pte_prot & PTE_W)) {
            uart_puts("[Segmentation fault]: Kill Process\n");
            do_exit(-1);
            return;
        }
        uart_puts("[Permission fault]: ");
        uart_hex(bad_va);
        uart_puts("\n");
        unsigned long old_pa = PTE_TO_PA(cur_pte);
        void *old_va = (void *)phys_to_virt(old_pa);
        if (frame_refcnt(old_va) == 1) {
            /* 唯一擁有者 → 放寬 W、不必複製 */
            vm_set_pte_writable_copy(me->pgd_pa, va_aligned, old_pa, v->pte_prot);
        } else {
            void *newp = buddy_alloc_pages(0);
            memcpy(newp, old_va, PAGE_SIZE);
            vm_set_pte_writable_copy(me->pgd_pa, va_aligned,
                                     virt_to_phys((unsigned long)newp), v->pte_prot);
            buddy_free_pages(old_va);   /* refcnt-- 老頁 */
        }
        return;
    }

    /* (2) store fault 且 vma 沒 W：寫只讀 region */
    if (needs_w && !(v->pte_prot & PTE_W)) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }

    /* (3) PTE 不存在 → demand paging */
    uart_puts("[Translation fault]: ");
    uart_hex(bad_va);
    uart_puts("\n");
    void *p = buddy_alloc_pages(0);
    memset(p, 0, PAGE_SIZE);
    map_pages(me->pgd_pa, va_aligned, PAGE_SIZE,
              virt_to_phys((unsigned long)p), v->pte_prot);
}
