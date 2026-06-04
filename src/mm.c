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
        struct vma *vma = list_entry(head->next, struct vma, link);
        list_del(&vma->link);
        dynalloc_free(vma);
    }
}

void vma_copy_list(struct list_head *dst, struct list_head *src) {
    init_list_head(dst);
    struct list_head *node = src->next;
    while (node != src) {
        struct vma *src_v = list_entry(node, struct vma, link);
        struct vma *new_vma = dynalloc_alloc(sizeof(*new_vma));
        *new_vma = *src_v;
        init_list_head(&new_vma->link);
        list_add_tail(&new_vma->link, dst);
        node = node->next;
    }
}

/* 找 [base, end) 區段內第一段連續 length 個 page-aligned bytes、不撞任何現有 vma */
static unsigned long vma_find_free(struct thread *me, unsigned long length) {
    unsigned long candidate = MMAP_AREA_BASE;
    while (candidate + length <= MMAP_AREA_END) {
        int conflict = 0;
        struct list_head *node = me->vma_list.next;
        while (node != &me->vma_list) {
            struct vma *vma = list_entry(node, struct vma, link);
            if (regions_overlap(candidate, candidate + length, vma->start, vma->end)) {
                candidate = round_up_page(vma->end);
                conflict = 1;
                break;
            }
            node = node->next;
        }
        if (!conflict) return candidate;
    }
    return 0;
}

static int vma_any_overlap(struct thread *me, unsigned long start, unsigned long end) {
    struct list_head *node = me->vma_list.next;
    while (node != &me->vma_list) {
        struct vma *vma = list_entry(node, struct vma, link);
        if (regions_overlap(start, end, vma->start, vma->end)) return 1;
        node = node->next;
    }
    return 0;
}

static void vma_insert_sorted(struct thread *me, struct vma *nv) {
    struct list_head *node = me->vma_list.next;
    while (node != &me->vma_list) {
        struct vma *vma = list_entry(node, struct vma, link);
        if (vma->start > nv->start) break;
        node = node->next;
    }
    list_add_tail(&nv->link, node);
}

/* 在指定 VA 建立 VMA 並插入 list，不做 find/populate。 */
struct vma *vma_create_at(struct thread *me, unsigned long start,
                                  unsigned long length, unsigned long pte_prot, int flags) {
    struct vma *nv = dynalloc_alloc(sizeof(*nv));
    nv->start    = start;
    nv->end      = start + length;
    nv->pte_prot = pte_prot;
    nv->flags    = flags;
    nv->src_va   = (void *)0;
    nv->src_size = 0;
    init_list_head(&nv->link);
    vma_insert_sorted(me, nv);
    return nv;
}

/* kernel 內部用：在 MMAP_AREA 找空洞配一段匿名 VMA，回傳 vma 指標（va = vma->start）。
 * 呼叫後可設 src_va / src_size 讓 do_page_fault 做 demand copy。 */
struct vma *vma_alloc(struct thread *me, unsigned long length, unsigned long pte_prot, int flags) {
    length = round_up_page(length);
    unsigned long va = vma_find_free(me, length);
    if (!va) return (void *)0;
    return vma_create_at(me, va, length, pte_prot, flags);
}

/* mmap 核心邏輯：hint / prot 轉換 / MAP_POPULATE，回傳配到的 VA。
 * addr  : NULL → kernel 自選；非 NULL 不對齊或撞區 → 改成 hint，自選
 * prot  : PROT_NONE / READ / WRITE / EXEC 任意或
 * flags : MAP_ANONYMOUS | MAP_POPULATE */
long do_mmap(struct thread *me, void *addr, unsigned long length, int prot, int flags) {
    if (length == 0) return -1;
    length = round_up_page(length);

    unsigned long hint_va = (unsigned long)addr;
    unsigned long start;
    if (!addr || (hint_va & (PAGE_SIZE - 1)) || vma_any_overlap(me, hint_va, hint_va + length))
        start = vma_find_free(me, length);
    else
        start = hint_va;
    if (!start) return -1;

    unsigned long pte_prot = PTE_V | PTE_U | PTE_A | PTE_D;
    if (prot & PROT_READ)  pte_prot |= PTE_R;
    if (prot & PROT_WRITE) pte_prot |= PTE_W;
    if (prot & PROT_EXEC)  pte_prot |= PTE_X;

    struct vma *vma = vma_create_at(me, start, length, pte_prot, flags);

    if (flags & MAP_POPULATE) {
        for (unsigned long va = start; va < start + length; va += PAGE_SIZE) {
            void *frame = buddy_alloc_pages(0);
            if (!frame) {
                list_del(&vma->link);
                dynalloc_free(vma);
                return -1;
            }
            memset(frame, 0, PAGE_SIZE);
            map_pages(me->pgd_pa, va, PAGE_SIZE,
                      virt_to_phys((unsigned long)frame), pte_prot);
        }
    }
    return (long)start;
}


/* Adv2: 查 va 落在哪個 vma；找不到回 NULL */
static struct vma *vma_lookup(struct thread *me, unsigned long va) {
    struct list_head *node = me->vma_list.next;
    while (node != &me->vma_list) {
        struct vma *vma = list_entry(node, struct vma, link);
        if (va >= vma->start && va < vma->end) return vma;
        node = node->next;
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
    if (vma_any_overlap(me, new_start, sv->start)) return (void *)0;
    sv->start = new_start;
    return sv;
}

/* fault cause 12 = inst fetch, 13 = load, 15 = store/AMO */
void do_page_fault(struct trap_frame *tf, unsigned long cause, unsigned long bad_va) {
    struct thread *me = kthread_get_current();
    struct vma *vma = vma_lookup(me, bad_va);

    if (!vma) {
        vma = try_expand_stack(me, bad_va);
    }
    if (!vma) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }

    int needs_w = (cause == 15);
    int needs_x = (cause == 12);
    /* exec faulting in NX region 直接 segfault */
    if (needs_x && !(vma->pte_prot & PTE_X)) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }

    unsigned long va_aligned = bad_va & ~(PAGE_SIZE - 1);
    unsigned long cur_pte = vm_lookup_pte(me->pgd_pa, bad_va);

    /* (1) PTE 存在 + store fault + PTE 沒 W → CoW (Adv3) 或寫只讀 vma */
    if ((cur_pte & PTE_V) && needs_w && !(cur_pte & PTE_W)) {
        if (!(vma->pte_prot & PTE_W)) {
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
            vm_set_pte_writable_copy(me->pgd_pa, va_aligned, old_pa, vma->pte_prot);
        } else {
            void *newp = buddy_alloc_pages(0);
            memcpy(newp, old_va, PAGE_SIZE);
            vm_set_pte_writable_copy(me->pgd_pa, va_aligned,
                                     virt_to_phys((unsigned long)newp), vma->pte_prot);
            buddy_free_pages(old_va);   /* refcnt-- 老頁 */
        }
        return;
    }

    /* (2) store fault 且 vma 沒 W：寫只讀 region */
    if (needs_w && !(vma->pte_prot & PTE_W)) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }

    /* load fault 但 vma 無 R → PROT_NONE，直接 segfault */
    if (!needs_w && !needs_x && !(vma->pte_prot & PTE_R)) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1);
        return;
    }

    /* (3) PTE 不存在 → demand paging */
    uart_puts("[Translation fault]: ");
    uart_hex(bad_va);
    uart_puts("\n");
    void *frame = buddy_alloc_pages(0);
    memset(frame, 0, PAGE_SIZE);
    if (vma->src_va) {
        /* code VMA：從 cpio 資料按頁 offset 複製正確內容 */
        unsigned long pg_off = va_aligned - vma->start;
        if ((int)pg_off < vma->src_size) {
            int copy_bytes = vma->src_size - (int)pg_off;
            if (copy_bytes > (int)PAGE_SIZE) copy_bytes = PAGE_SIZE;
            memcpy(frame, (const char *)vma->src_va + pg_off, copy_bytes);
        }
    }
    map_pages(me->pgd_pa, va_aligned, PAGE_SIZE,
              virt_to_phys((unsigned long)frame), vma->pte_prot);
}
