#include "shell.h"
#include "uart.h"
#include "bootinfo.h"
#include "buddy.h"
#include "dynalloc.h"
#include "mem_reserve.h"
#include "startup.h"
#include "timer.h"
#include "plic.h"
#include "trap.h"

#define STARTUP_HEAP_SLACK (128UL * 1024) // 128KB slack for alignment padding

void start_kernel(unsigned long hart_id, const void* fdt, unsigned long reloc_addr) {
    uart_init(fdt);
    bootinfo_init(fdt, reloc_addr);

    // Phase 1: 登記所有 reserved 區段。memory_reserve 只把區間 push 到靜態陣列，不依賴任何 allocator
    // OpenSBI 的區段不能硬編碼，若板子 /reserved-memory 有列就由 memory_reserve_fdt_reserved_mem 處理；
    // 否則 startup heap 會透過 lower-bound 避開（見 startup_auto_init），但 buddy 仍可能管到那段 RAM
    memory_reserve(g_kernel_start, g_kernel_end - g_kernel_start);                        // kernel image
    memory_reserve((unsigned long)g_fdt_base, g_fdt_size);                                // dtb blob
    memory_reserve((unsigned long)g_initrd_base,
                   g_initrd_end - (unsigned long)g_initrd_base);                          // initramfs
    memory_reserve_fdt_reserved_mem(fdt);                                                 // /reserved-memory 子節點

    // Phase 2: 告訴 buddy 可用 RAM 範圍（只設 runtime 變數、還不配任何 metadata）
    buddy_register_range(g_mem_base, g_mem_size);

    // Phase 3: 依 total_pages 精算 heap size（frame_array + page_descriptors + slack），掃 reserved 列表找首塊可用空洞初始化
    unsigned long heap_size = sizeof(struct frame) * g_buddy_total_pages
                            + dynalloc_metadata_bytes(g_buddy_total_pages)
                            + STARTUP_HEAP_SLACK;
    startup_init(heap_size);

    // Phase 4: 交棒 buddy / dynalloc，它們會從 startup heap 動態拿 frame_array / page_descriptors
    buddy_init();
    dynalloc_init();

    // trap 子系統總開關：開 sstatus.SIE，各 driver 再自行開對應 sie.X
    trap_init();

    // timer
    timer_init();

    // PLIC（先做 driver smoke test，還沒接 UART IRQ）
    plic_init(fdt, hart_id);

    shell_init();
    while (1) {
        shell();
    }
}
