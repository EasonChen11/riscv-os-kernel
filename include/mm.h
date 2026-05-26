#ifndef MM_H
#define MM_H

#include "list.h"
#include "trap.h"

struct thread;

/* mmap user-facing constants（rst） */
#define PROT_NONE       0
#define PROT_READ       1
#define PROT_WRITE      2
#define PROT_EXEC       4

#define MAP_ANONYMOUS   0x20
#define MAP_POPULATE    0x8000

/* user mmap 找空洞用的 base；避開 VA 0..image 與 user stack page */
#define MMAP_AREA_BASE  0x10000000UL
#define MMAP_AREA_END   0x3fffff0000UL   /* user stack base 之下 */


struct vma {
    unsigned long    start;     /* page-aligned */
    unsigned long    end;       /* page-aligned，exclusive */
    unsigned long    pte_prot;  /* PTE flag set，含 V/U/A/D 與 R/W/X */
    int              flags;     /* MAP_ANONYMOUS | MAP_POPULATE | ... */
    struct list_head link;      /* thread->vma_list，按 start 升冪 */
    const void      *src_va;    /* code VMA：cpio 資料 kernel VA；anonymous 為 0 */
    int              src_size;  /* code VMA：binary 大小（bytes）；anonymous 為 0 */
};

void  vma_init_list(struct list_head *head);
void  vma_free_all(struct list_head *head);
void  vma_copy_list(struct list_head *dst, struct list_head *src);
struct vma *vma_create_at(struct thread *me, unsigned long start, unsigned long length, unsigned long pte_prot, int flags);
struct vma *vma_alloc(struct thread *me, unsigned long length, unsigned long pte_prot, int flags);

long  do_mmap(struct thread *me, void *addr, unsigned long length, int prot, int flags);

/* Adv2: trap.c 的 cause 12/13/15 都 dispatch 來這裡。
 * 在 vma 內 → demand paging 補上 page；不在 → segfault 殺 process。 */
void  do_page_fault(struct trap_frame *tf, unsigned long cause, unsigned long bad_va);

#endif
