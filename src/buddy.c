#include "buddy.h"
#include "uart.h"
#include "mem_reserve.h"
#include "startup.h"

static struct frame *frame_array; // 這個陣列的每一個元素對應一個 page，紀錄該 page 的狀態（order、allocated）和 free list 的鏈結資訊
static struct list_head free_lists[BUDDY_MAX_ORDER + 1];
static int free_counts[BUDDY_MAX_ORDER + 1];   // 各 order 目前 free block 數量
int step = 1; // 用於追蹤呼叫次數（跨模組共用 log 編號）

void buddy_reset_step(void) {
    step = 1;
}

unsigned long g_buddy_base_addr;
unsigned long g_buddy_total_pages;


static inline unsigned long get_buddy_index(unsigned long index, int order) {
    return index ^ (1UL << order);
}

// O(1) frame <-> page index helpers (file-private; depend on frame_array)
static inline unsigned long frame_to_idx(struct frame *frame) {
    return frame - frame_array;
}

static inline struct frame *idx_to_frame(unsigned long idx) {
    return &frame_array[idx];
}

static void dump_counts(void) {
    uart_puts("[Buddy] counts:");
    for (int i = 0; i <= BUDDY_MAX_ORDER; i++) {
        uart_puts(" o");
        uart_int(i);
        uart_puts("=");
        uart_int(free_counts[i]);
    }
    uart_puts("\n");
}

// [-] Remove page N from order K. Range of pages: [N, N + 2^K - 1]                                                                        
static void log_remove(unsigned long index, int order) {                                                                             
    uart_int(step++);                                                                                                                      
    uart_puts(". Removing a block from the free list:\n");                                                                                 
    uart_puts("    [-] Remove page ");
    uart_int(index);                                                                                                                       
    uart_puts(" from order ");                            
    uart_int(order);                                                                                                                       
    uart_puts(". Range of pages: [");
    uart_int(index);                                                                                                                       
    uart_puts(", ");                                      
    uart_int(index + (1UL << order) - 1);
    uart_puts("]\n");                                                                                                                      
}
                                                                                                                                            
// [+] Add page N to order K. Range of pages: [N, N + 2^K - 1]                                                                             
static void log_add(unsigned long index, int order) {
    uart_int(step++);                                                                                                                      
    uart_puts(". Adding a block to the free list:\n");    
    uart_puts("    [+] Add page ");                                                                                                        
    uart_int(index);
    uart_puts(" to order ");                                                                                                               
    uart_int(order);                                      
    uart_puts(". Range of pages: [");
    uart_int(index);                                                                                                                       
    uart_puts(", ");
    uart_int(index + (1UL << order) - 1);                                                                                                  
    uart_puts("]\n");                                     
}
                                                                                                                                            
// [Page] Allocate 0xADDR at order K, page N
static void log_alloc(unsigned long addr, int order, unsigned long index) {                                                          
    uart_int(step++);                                                                                                                      
    uart_puts(". Allocating a page:\n");
    uart_puts("    [Page] Allocate ");
    uart_hex(addr);
    uart_puts(" at order ");
    uart_int(order);                                                                                                                       
    uart_puts(", page ");                                 
    uart_int(index);                                                                                                                       
    uart_puts("\n");
} 

static void log_free(unsigned long addr, int order, unsigned long index) {
    uart_int(step++);                                                                                                                      
    uart_puts(". Freeing a page:\n");
    uart_puts("    [Page] Free ");
    uart_hex(addr);
    uart_puts(" at order ");
    uart_int(order);                                                                                                                       
    uart_puts(", page ");                                 
    uart_int(index);                                                                                                                       
    uart_puts("\n");
}

static void log_merge(unsigned long index1, unsigned long index2, int order) {
    uart_int(step++);                                                                                                                      
    uart_puts(". Merging two blocks:\n");
    uart_puts("    [Merge] Merge page ");
    uart_int(index1);
    uart_puts(" and page ");
    uart_int(index2);
    uart_puts(" into order ");
    uart_int(order);                                                                                                                       
    uart_puts("\n");
}

static void freelist_add(struct frame *frame, int order) {
    frame->order = order;
    frame->allocated = 0;            // free 起點
    unsigned long index = frame_to_idx(frame);
    list_add(&frame->freelist, &free_lists[order]);
    free_counts[order]++;
    // log_add(index, order); // Log the addition of the block to the free list
}

static void freelist_del(struct frame *frame) {
    int order = frame->order;
    unsigned long index = frame_to_idx(frame);
    list_del(&frame->freelist);
    free_counts[order]--;
    // log_remove(index, order); // Log the removal of the block from the free list
}

void buddy_register_range(unsigned long base_addr, unsigned long size) {
    g_buddy_base_addr = base_addr;
    g_buddy_total_pages = size / PAGE_SIZE;
}
// 找出在 page index `start` 開頭、最大可放下的 order block 
static int max_block_order(unsigned long start) {
    int order = BUDDY_MAX_ORDER;
    while (order > 0) {
        unsigned long block_size = 1UL << order;
        if ((start & (block_size - 1)) != 0) { // start % block_size
            order--;
            continue; // start 必須是 block 大小的倍數，避免非法的 block 分布在 page 中間
        }
        if(start+block_size > g_buddy_total_pages) {
            order--; // 超出 buddy 管理範圍的部分直接截斷，仍嘗試較小的 block
            continue;
        }
        int have_reserved = 0;
        for (unsigned long i = start; i < start + block_size; i++) {
            if (memory_is_reserved(i)) {
                have_reserved = 1;
                break;
            }
        }
        if (have_reserved) {
            order--; // 這個 block 中有被 reserve 的頁面 → 嘗試較小的 block
            continue;
        }
        break; // 找到一個合法的 block
    }
    return order;
}

void buddy_init(void) {
    // 由 startup allocator 配置 frame_array（此時所有 reserved 區段已登記、heap 已設好）
    // _Alignof 回傳結構的對齊需求，語言保證為 2 的冪；用 sizeof 當 align 是錯的
    frame_array = (struct frame*)startup_alloc(
        sizeof(struct frame) * g_buddy_total_pages,
        _Alignof(struct frame));
    // Initialize free lists
    for (int i = 0; i <= BUDDY_MAX_ORDER; i++) {
        init_list_head(&free_lists[i]);
        free_counts[i] = 0; // Initialize free block counts
    }
    for (unsigned long i = 0; i < g_buddy_total_pages; i++) {
        frame_array[i].order = BUDDY_F; // Mark all frames as non-head
        frame_array[i].allocated = 0;
        frame_array[i].refcnt = 0;
    }
    unsigned long start = 0;
    while (start < g_buddy_total_pages) {
        if(memory_is_reserved(start)) {
            start++;
            continue; // Skip reserved pages
        }
        int order = max_block_order(start); // 找出在 start 開頭、最大可放下的 order block
        freelist_add(idx_to_frame(start), order);
        start += (1UL << order); // 跳過這個 block 的頁面
    }
}

void *buddy_alloc_pages(int order) {
    if (order < 0 || order > BUDDY_MAX_ORDER) {
        return (void*)0; // Invalid order
    }
    // Find the smallest available block
    int current_order = order;
    while (current_order <= BUDDY_MAX_ORDER && list_empty(&free_lists[current_order])) {
        current_order++;
    }
    if (current_order > BUDDY_MAX_ORDER) {
        return (void*)0; // No suitable block found
    }

    // Split blocks until we reach the desired order
    struct list_head *free_node = free_lists[current_order].next;
    struct frame *target_frame = list_entry(free_node, struct frame, freelist);
    freelist_del(target_frame);

    while (current_order > order) {
        current_order--;
        unsigned long buddy_index = get_buddy_index(frame_to_idx(target_frame), current_order);
        freelist_add(idx_to_frame(buddy_index), current_order);
    }
    target_frame->order = order;
    target_frame->allocated = 1;            // 標記為 allocated 起點
    target_frame->refcnt = 1;               // CoW：起點 1 個 owner

    unsigned long index = frame_to_idx(target_frame);
    unsigned long addr = buddy_page_idx_to_addr(index);
    // log_alloc(addr, order, index); // Log the allocation of the page
    // dump_counts();  // debug 用，task pool 每 heartbeat 都會動 buddy，噪音太大
    return (void *)(addr);
}

void buddy_free_pages(void *addr) {
    unsigned long index = buddy_addr_to_page_idx((unsigned long)addr);
    if(index >= g_buddy_total_pages) {
        return; // Invalid address
    }
    struct frame *self = idx_to_frame(index);
    int order = self->order;
    if (order < 0 || order > BUDDY_MAX_ORDER) {
        return; // Invalid block
    }
    if (!self->allocated) {
        return; // 重複 free 防護：這個 frame 不是 allocated 起點
    }
    /* CoW：refcnt-- 後 > 0 表示還有其他 PGD 共享，先不真正歸還 */
    if (self->refcnt > 1) {
        self->refcnt--;
        return;
    }
    self->refcnt = 0;
    self->allocated = 0;

    while (order < BUDDY_MAX_ORDER) {
        unsigned long buddy_index = get_buddy_index(index, order);
        if (buddy_index >= g_buddy_total_pages) break;
        struct frame *buddy_frame = idx_to_frame(buddy_index);
        if (buddy_frame->allocated || buddy_frame->order != order) {
            break; // buddy 是 allocated、F/X、或不同 order → 不可合併
        }
        freelist_del(buddy_frame); // 從 free list 移除 buddy
        // log_merge(index, buddy_index, order + 1); // Log the merging of blocks
        if (buddy_index < index) {
            index = buddy_index; // Keep the lower index as the head of the merged block
        }
        order++;
    }

    unsigned long block_addr = buddy_page_idx_to_addr(index);
    // log_free(block_addr, order, index); // Log the freeing of the page

    freelist_add(idx_to_frame(index), order);
    // dump_counts();  // 同上：heartbeat 觸發的 task slot free 會撞到，留給手動除錯時呼叫
}
/* CoW：多一個 PGD 引用這頁 → refcnt++ */
void frame_get(void *va) {
    unsigned long idx = buddy_addr_to_page_idx((unsigned long)va);
    if (idx >= g_buddy_total_pages) return;
    frame_array[idx].refcnt++;
}

unsigned int frame_refcnt(void *va) {
    unsigned long idx = buddy_addr_to_page_idx((unsigned long)va);
    if (idx >= g_buddy_total_pages) return 0;
    return frame_array[idx].refcnt;
}
