#include "mem_reserve.h"
#include "uart.h"
#include "buddy.h"
#include "fdt.h"

#define MAX_RESERVED_REGIONS 32

struct reserved_region {
    unsigned long start; // 已向下對齊到 PAGE_SIZE
    unsigned long end;   // 已向上對齊到 PAGE_SIZE
};

static struct reserved_region regions[MAX_RESERVED_REGIONS];
static int region_count = 0;

void log_reserved_regions(unsigned long aligned_start, unsigned long aligned_end) {
    uart_puts("[Reserve] Reserve address [");
    uart_hex(aligned_start);
    uart_puts(", ");
    uart_hex(aligned_end);
    uart_puts(")\n");
}

// 將 [start, start+size) 涵蓋的頁面登記為 reserved：start 向下、end 向上對齊到 page boundary，
// 再 push 進 regions 陣列。不依賴任何 allocator，可在 buddy / startup 初始化之前呼叫
void memory_reserve(unsigned long start, unsigned long size) {
    if (size == 0) return;
    unsigned long aligned_start = start & ~(PAGE_SIZE - 1);
    unsigned long aligned_end   = (start + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (region_count >= MAX_RESERVED_REGIONS) {
        uart_puts("[Reserve] Error: regions table full\n");
        return;
    }
    regions[region_count].start = aligned_start;
    regions[region_count].end   = aligned_end;
    region_count++;

    // log_reserved_regions(aligned_start, aligned_end);
}

void memory_reserve_fdt_reserved_mem(const void* fdt) {
    int offset = fdt_path_offset(fdt, "/reserved-memory");
    if (offset < 0) {
        uart_puts("[Reserve] /reserved-memory node not found in FDT\n");
        return;
    }
    for (int i = fdt_first_subnode(fdt, offset); i >= 0; i = fdt_next_subnode(fdt, i)) {
        unsigned long base, size;
        if (fdt_get_reg(fdt, offset, i, 0, &base, &size) < 0 || size == 0) {
            continue;
        }
        memory_reserve(base, size);
    }
}

// 呼叫者必須確保 buddy_register_range 已設好 g_buddy_base_addr
int memory_is_reserved(unsigned long page_idx) {
    /* regions[] 全是 PA；不能用 buddy_page_idx_to_addr（lab6 起回傳 VA） */
    unsigned long addr = g_buddy_base_addr + page_idx * PAGE_SIZE;
    for (int i = 0; i < region_count; i++) {
        if (addr >= regions[i].start && addr < regions[i].end) { // 如果 addr 落在任何一個 reserved 區間內
            return 1;
        }
    }
    return 0;
}

// 在 [search_lo, search_hi) 內找第一個連續 size bytes、對齊 align 的空洞，避開所有 reserved regions
unsigned long memory_find_free(unsigned long search_lo, unsigned long search_hi,
                               unsigned long size, unsigned long align) {
    unsigned long candidate = (search_lo + align - 1) & ~(align - 1);
    while (candidate + size <= search_hi) {
        int conflict = 0;
        for (int i = 0; i < region_count; i++) {
            // 區間 overlap 條件：candidate < r.end && candidate+size > r.start
            if (candidate < regions[i].end && candidate + size > regions[i].start) {
                // 跳到這個 region 結束之後的下一個對齊點再試
                candidate = (regions[i].end + align - 1) & ~(align - 1);
                conflict = 1;
                break;
            }
        }
        if (!conflict) return candidate;
    }
    return 0;
}
