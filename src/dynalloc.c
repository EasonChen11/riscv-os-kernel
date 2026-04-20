#include "dynalloc.h"
#include "buddy.h"
#include "list.h"
#include "uart.h"
#include "startup.h"

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

void *dynalloc_alloc(unsigned int size) {
    // size 超過最大 chunk 大小，go to buddy allocator
    if (size > DA_MAX_CHUNK_SIZE) {
        unsigned int pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE; // 向上取整需要的 page 數量
        int order = 0;
        while ((1U << order) < pages_needed) {
            order++;
        }
        if(order > BUDDY_MAX_ORDER) {
            return (void*)0; // 超過 buddy allocator 的最大 order
        }
        return buddy_alloc_pages(order);
    }

    // 找到適合的 pool
    int pool_index = -1;
    for (int i = 0; i < DA_NUM_POOLS; i++) {
        if (size <= pools[i].chunk_size) {
            pool_index = i;
            break;
        }
    }
    if (pool_index == -1) {
        return (void*)0; // 沒有適合的 pool
    }

    struct chunk_pool *pool = &pools[pool_index];
    if (list_empty(&pool->free_chunks)) {
        // 沒有空閒 chunk，分配一個新的 page 並切割成 chunk
        void *page = buddy_alloc_pages(0); // 分配 1 page
        if (page == (void*)0) {
            return (void*)0; // 分配失敗
        }
        unsigned long page_index = buddy_addr_to_page_idx((unsigned long)page);
        page_descriptors[page_index].pool_index = pool_index;
        page_descriptors[page_index].in_use = 0;

        // 切割 page 成 chunk 並加入 free list O(n)
        for (unsigned int offset = 0; offset < PAGE_SIZE; offset += pool->chunk_size) {
            struct list_head *chunk_node = (struct list_head *)((char *)page + offset); // 前 16 bytes 用於 list_head
            list_add(chunk_node, &pool->free_chunks);
        }
    }

    // 從 free list 中分配一個 chunk
    struct list_head *chunk_node = pool->free_chunks.next;
    list_del(chunk_node);
    page_descriptors[buddy_addr_to_page_idx((unsigned long)chunk_node)].in_use++;
    log_chunk_alloc((unsigned long)chunk_node, pool->chunk_size);
    return (void *)chunk_node;
}

void dynalloc_free(void *ptr) {
    if (ptr == (void*)0) {
        return; // 無效指標
    }
    unsigned long addr = (unsigned long)ptr;
    unsigned long page_index = buddy_addr_to_page_idx(addr);
    if (page_index >= g_buddy_total_pages) {
        return; // 超出 buddy allocator 管理的範圍
    }
    int pool_index = page_descriptors[page_index].pool_index;
    if (pool_index == -1) {
        // 不是 chunk page，直接交給 buddy allocator 處理
        buddy_free_pages((void *)addr);
        return;
    }

    struct chunk_pool *pool = &pools[pool_index];
    struct list_head *chunk_node = (struct list_head *)ptr;
    list_add(chunk_node, &pool->free_chunks);
    page_descriptors[page_index].in_use--;
    log_chunk_free(addr, pool->chunk_size);

    // 如果該 page 上的 chunk 都是 free 的，回收整個 page 給 buddy allocator
    if (page_descriptors[page_index].in_use == 0) {
        unsigned long page_addr = buddy_page_idx_to_addr(page_index);
        unsigned long page_end  = page_addr + PAGE_SIZE;
        // 回收 page 上所有 free chunks（拆出 pool->free_chunks）
        struct list_head *chunk = pool->free_chunks.next;
        while (chunk != &pool->free_chunks) {
            struct list_head *next_chunk = chunk->next;
            if ((unsigned long)chunk >= page_addr && (unsigned long)chunk < page_end) {
                list_del(chunk);
            }
            chunk = next_chunk;
        }
        buddy_free_pages((void *)page_addr);
        page_descriptors[page_index].pool_index = -1; // 重置為非 chunk page
    }
}