#include "dynalloc.h"
#include "buddy.h"
#include "list.h"
#include "uart.h"
#include "startup.h"
#include "irq.h"

extern int step; // 來自 buddy.c，跨模組共用 log 編號

struct chunk_pool {
    unsigned int chunk_size;
    struct list_head free_chunks;
};

struct page_descriptor {
    int pool_index; // -1 表示不是 chunk page, 0~(DA_NUM_POOLS-1) 表示屬於哪個 pool
    int in_use; // 此 page 還有幾個 chunk 處於 allocated
};
static struct chunk_pool pools[DA_NUM_POOLS];
static struct page_descriptor *page_descriptors; // 每個 page 的描述資訊
static const unsigned int pool_sizes[DA_NUM_POOLS] = {16, 32, 48, 64, 96, 128, 256, 512, 1024, 2048};

// N. Allocating a chunk:
//     [Chunk] Allocate 0xADDR at chunk size K
static void log_chunk_alloc(unsigned long addr, unsigned int chunk_size) {
    uart_int(step++);
    uart_puts(". Allocating a chunk:\n");
    uart_puts("    [Chunk] Allocate ");
    uart_hex(addr);
    uart_puts(" at chunk size ");
    uart_int(chunk_size);
    uart_puts("\n");
}

// N. Freeing a chunk:
//     [Chunk] Free 0xADDR at chunk size K
static void log_chunk_free(unsigned long addr, unsigned int chunk_size) {
    uart_int(step++);
    uart_puts(". Freeing a chunk:\n");
    uart_puts("    [Chunk] Free ");
    uart_hex(addr);
    uart_puts(" at chunk size ");
    uart_int(chunk_size);
    uart_puts("\n");
}

unsigned long dynalloc_metadata_bytes(unsigned long total_pages) {
    return sizeof(struct page_descriptor) * total_pages;
}

void dynalloc_init(void) {
    // _Alignof 回傳 struct 的對齊需求（= 內部最嚴格欄位的對齊），保證是 2 的冪；
    // 用 sizeof 當對齊值是錯的，因為 sizeof 是總大小（含 padding），加欄位後可能不是 2 的冪
    page_descriptors = (struct page_descriptor*)startup_alloc(
        sizeof(struct page_descriptor) * g_buddy_total_pages,
        _Alignof(struct page_descriptor));
    for (int i = 0; i < DA_NUM_POOLS; i++) {
        pools[i].chunk_size = pool_sizes[i];
        init_list_head(&pools[i].free_chunks);
    }
    for (unsigned long i = 0; i < g_buddy_total_pages; i++) {
        page_descriptors[i].pool_index = -1; // -1 表示不是 chunk page
        page_descriptors[i].in_use = 0; // 0 表示該 page 上的 chunk 都是 free 的
    }
}

// dynalloc 在 lab5 加 preempt 後變成 SMP-style shared resource：
//   - kthread_create / do_fork 從 thread context (SIE=1) 呼叫 dynalloc_alloc
//   - kill_zombies / drain_tasks / sleep_wake 也呼叫 dynalloc_alloc/free
// 任何 list_add / list_del / in_use RMW 中間都可能被 preempt timer 中斷，
// 切到別 thread 對同一個 pool 操作，醒回來時 free_chunks 鏈、in_use 計數都
// 處於半成品狀態，後續走 list 會踩野指標。整個函式包 irq_save 才安全。
//
// nested 安全：buddy_alloc_pages / buddy_free_pages 內部沒做 SIE 操作，所以
// 從 dynalloc 進去 SIE 維持 0 不變；返回後我們 restore 自己的 flags。
void *dynalloc_alloc(unsigned int size) {
    unsigned long flags = irq_save();
    void *result = (void*)0;

    if (size > DA_MAX_CHUNK_SIZE) {
        unsigned int pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        int order = 0;
        while ((1U << order) < pages_needed) order++;
        if (order > BUDDY_MAX_ORDER) goto out;
        result = buddy_alloc_pages(order);
        goto out;
    }

    int pool_index = -1;
    for (int i = 0; i < DA_NUM_POOLS; i++) {
        if (size <= pools[i].chunk_size) { pool_index = i; break; }
    }
    if (pool_index == -1) goto out;

    struct chunk_pool *pool = &pools[pool_index];
    if (list_empty(&pool->free_chunks)) {
        void *page = buddy_alloc_pages(0);
        if (page == (void*)0) goto out;
        unsigned long page_index = buddy_addr_to_page_idx((unsigned long)page);
        page_descriptors[page_index].pool_index = pool_index;
        page_descriptors[page_index].in_use = 0;
        for (unsigned int offset = 0; offset < PAGE_SIZE; offset += pool->chunk_size) {
            struct list_head *chunk_node = (struct list_head *)((char *)page + offset);
            list_add(chunk_node, &pool->free_chunks);
        }
    }

    struct list_head *chunk_node = pool->free_chunks.next;
    list_del(chunk_node);
    page_descriptors[buddy_addr_to_page_idx((unsigned long)chunk_node)].in_use++;
    result = (void *)chunk_node;

out:
    irq_restore(flags);
    return result;
}

void dynalloc_free(void *ptr) {
    if (ptr == (void*)0) return;

    unsigned long flags = irq_save();
    unsigned long addr = (unsigned long)ptr;
    unsigned long page_index = buddy_addr_to_page_idx(addr);
    if (page_index >= g_buddy_total_pages) goto out;

    int pool_index = page_descriptors[page_index].pool_index;
    if (pool_index == -1) {
        buddy_free_pages((void *)addr);
        goto out;
    }

    struct chunk_pool *pool = &pools[pool_index];
    struct list_head *chunk_node = (struct list_head *)ptr;
    list_add(chunk_node, &pool->free_chunks);
    page_descriptors[page_index].in_use--;

    // [DISABLED, 2026-04-29] 整 page 回收 cleanup loop
    //
    // alloc/free 的 irq_save 已修正單純的 preempt race；但 cleanup loop 在
    // sleep_ctx (16-byte pool) 高頻 alloc/free 場景下仍會踩雷：當 cleanup
    // 把 page 還回 buddy 並把 page_descriptors[].pool_index = -1，timer queue
    // 中已 add_timer_ticks 但未 fire 的 sleep_wake callback 仍持有該 page 內
    // chunk 的 stale pointer。callback fire 後走 dynalloc_free 的「pool_index
    // == -1」分支，把非 page-aligned 的 chunk address 餵給 buddy_free_pages，
    // buddy 內部 frame_array 算錯 idx → list 結構壞 → 死或 fault。
    //
    // 真正的修需要其中一個：
    //   (a) timer queue 加 cancel API：thread 死時 cancel 它的 sleep_wake
    //   (b) sleep_ctx 嵌進 thread struct（thread 死時自然 free，不依賴 dynalloc）
    //   (c) dynalloc 加 ref count，cleanup 不只看 in_use 還看外部引用
    //
    // BE2 grading #5「fork-exit 不漏 page」可能扣幾分；BE3 + Adv1 demo 體驗
    // 優先，根因延到 lab6 一起 audit。原本實作保留在 #if 0 內當 reference。
#if 0
    // 保留註解
    if (page_descriptors[page_index].in_use == 0) { 
        unsigned long page_addr = buddy_page_idx_to_addr(page_index);
        unsigned long page_end  = page_addr + PAGE_SIZE;
        struct list_head *chunk = pool->free_chunks.next;
        while (chunk != &pool->free_chunks) {
            struct list_head *next_chunk = chunk->next;
            if ((unsigned long)chunk >= page_addr && (unsigned long)chunk < page_end) {
                list_del(chunk);
            }
            chunk = next_chunk;
        }
        buddy_free_pages((void *)page_addr);
        page_descriptors[page_index].pool_index = -1;
    }
#endif
    (void)page_index;

out:
    irq_restore(flags);
}