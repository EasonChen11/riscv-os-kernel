#include "startup.h"
#include "uart.h"
#include "bootinfo.h"
#include "mem_reserve.h"
#include "pgtable.h"

static unsigned long heap_start, heap_end, heap_current; // heap_current 是下一個可用位址，已分配的範圍是 [heap_start, heap_current)

static void startup_range(void *start, void *end) {
    heap_start = (unsigned long)start;
    heap_end = (unsigned long)end;
    heap_current = heap_start;
}

static void log_startup_heap(unsigned long heap_size) {
    uart_puts("[Startup] Heap at [");
    uart_hex(heap_start);
    uart_puts(", ");
    uart_hex(heap_start + heap_size);
    uart_puts(")\n");
}

// 在可用 RAM 內、避開所有已 reserve 的區間，找第一塊 size bytes、4KB 對齊的空洞作為 heap。
// 搜尋下界取 max(g_kernel_end, g_fdt_base+g_fdt_size, g_initrd_end)——全部來自 linker 符號或 /chosen DT，
// 避免硬編碼 OpenSBI 位址；自然跳過位於 RAM 起點的 firmware 區段
void startup_init(unsigned long heap_size) {
    heap_size = (heap_size + 0xFFFUL) & ~0xFFFUL; // 向上對齊到 4KB
    // 從最小的 end 開始掃：kernel 被 relocate 到 RAM 頂端，若取 max 會使搜尋起點過高；
    // 取 min 後由 memory_find_free 跳過 kernel / fdt / initrd / /reserved-memory 等已登記區段，
    // 找出中段的可用空洞。結果再 clamp 到 g_mem_base 以上避免落在 RAM 之外
    /* lab6: 全部以 PA 算範圍（mem_reserve 是 PA-domain），最後 heap 暴露 VA */
    unsigned long lower_pa = virt_to_phys(g_kernel_end);
    unsigned long fdt_end_pa = virt_to_phys((unsigned long)g_fdt_base) + g_fdt_size;
    unsigned long initrd_end_pa = virt_to_phys(g_initrd_end);
    if (fdt_end_pa    < lower_pa) lower_pa = fdt_end_pa;
    if (initrd_end_pa < lower_pa) lower_pa = initrd_end_pa;
    if (lower_pa < g_mem_base) lower_pa = g_mem_base;
    unsigned long start_pa = memory_find_free(lower_pa, g_mem_base + g_mem_size, heap_size, 0x1000);
    if (start_pa == 0) {
        uart_puts("[Startup] Error: no free region large enough for heap\n");
        while (1) {}
    }
    memory_reserve(start_pa, heap_size);
    unsigned long start_va = phys_to_virt(start_pa);
    startup_range((void*)start_va, (void*)(start_va + heap_size));
    // log_startup_heap(heap_size);
}

void *startup_alloc(unsigned long size, unsigned long align) {
    unsigned long aligned_current = (heap_current + align - 1) & ~(align - 1); // 向上對齊到下一個 align 的倍數，確保回傳的地址符合對齊要求
    if (aligned_current + size > heap_end) {
        uart_puts("[Startup] Out of memory\n");
        while (1) {}
    }
    heap_current = aligned_current + size;
    return (void*)aligned_current;
}
