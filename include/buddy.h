#ifndef BUDDY_H
#define BUDDY_H

#include "list.h"
#include "pgtable.h"   /* PAGE_SIZE 與 PAGE_OFFSET 由此提供 */

#define BUDDY_MAX_ORDER 16
// #define BUDDY_MAX_PAGES (1 << BUDDY_MAX_ORDER) // 16K pages = 64MB total memory 已經使用 g_buddy_total_pages 來動態決定

#define BUDDY_F (-1)   // free block 的非起點頁（不在任何 free list 上）
#define BUDDY_X (-2)   // allocated block 的非起點頁 

struct frame {
    int order;                  // The order of the block (0 for 1 page, 1 for 2 pages, etc.)
    int allocated;              // 0 = free 起點 / 中段; 1 = allocated 起點
    unsigned int refcnt;        // CoW：>1 表示多個 PGD 共享此頁；alloc 設 1，free 是 refcnt--
    struct list_head freelist;  // List node for linking frames in the free list
};

extern unsigned long g_buddy_base_addr;
extern unsigned long g_buddy_total_pages;

void buddy_init(void);
void *buddy_alloc_pages(int order);
void buddy_free_pages(void *addr);
void buddy_register_range(unsigned long base_addr, unsigned long size);
void buddy_reset_step(void); // 重置 log step 編號為 1，供外部測試腳本在跑前呼叫

/* CoW refcount API：
 *   frame_get(va)  → 共享者多一個，refcnt++（va = buddy 配出的 VA）
 *   frame_refcnt() → 查目前 refcnt
 *   buddy_free_pages() 自帶 refcnt--；refcnt==0 才實際歸還。
 *   一般 alloc 後 refcnt=1，無需手動呼叫 frame_get。 */
void frame_get(void *va);
unsigned int frame_refcnt(void *va);

/* g_buddy_base_addr 為 PA；外部 helper 公開 VA。
 * Sv39 啟用後 kernel 一律透過 PAGE_OFFSET 的 linear map 存取頁框，
 * buddy_alloc_pages 回傳 VA、buddy_free_pages 也吃 VA。 */
static inline unsigned long buddy_addr_to_page_idx(unsigned long va) {
    return (va - PAGE_OFFSET - g_buddy_base_addr) / PAGE_SIZE;
}

static inline unsigned long buddy_page_idx_to_addr(unsigned long idx) {
    return PAGE_OFFSET + g_buddy_base_addr + idx * PAGE_SIZE;
}

#endif // __BUDDY_H__