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
#include "task.h"
#include "schedule.h"
#include "exec.h"
#include "thread.h"
#include "pgtable.h"
// schedule.h 已 declare start_preempt

#define STARTUP_HEAP_SLACK (128UL * 1024) // 128KB slack for alignment padding

static void foo(void) {
    for (int i = 0; i < 5; i++) {
        uart_puts("Thread id: ");
        uart_hex(kthread_get_current()->pid);
        uart_puts(" iter ");
        uart_hex(i);
        uart_puts("\n");
        for (volatile int j = 0; j < 50000000; j++) ;
        schedule();
    }
    thread_exit();
}

void start_kernel(unsigned long hart_id, const void* fdt, unsigned long reloc_addr) {
    // Phase 0: 只解 DTB 與暫存器位址，此階段不可呼叫 uart_puts（interrupt 模式下 ring 尚無 drainer）
    uart_init(fdt);
    bootinfo_init(fdt, reloc_addr);

    /* For intterupt:
    task_init 必須先於 trap_init：do_trap 結尾會呼叫 drain_tasks，
    kernel 自我搬遷使 .data 靜態 self-pointing 失效，必須 runtime init_list_head。 */
    task_init();              // init_list_head(&task_list) + task_init 的其他初始化
    trap_init();              // 開 sstatus.SIE
    plic_init(fdt, hart_id);  // 設 PLIC base / threshold / sie.SEIE
    uart_irq_init();          // 設 UART IER/MCR/FCR + PLIC enable；interrupt 路徑就緒

    // 從這裡開始 uart_puts 可用（interrupt 模式下由 UART ISR drain TX ring）
    uart_init_print_info();

    // Phase 1: 登記所有 reserved 區段。memory_reserve 只把區間 push 到靜態陣列，不依賴任何 allocator
    // OpenSBI 的區段不能硬編碼，若板子 /reserved-memory 有列就由 memory_reserve_fdt_reserved_mem 處理；
    // 否則 startup heap 會透過 lower-bound 避開（見 startup_auto_init），但 buddy 仍可能管到那段 RAM
    /* mem_reserve / buddy 全是 PA-domain；kernel/fdt/initrd 在 lab6 都存 VA，需先轉 PA */
    memory_reserve(virt_to_phys(g_kernel_start), g_kernel_end - g_kernel_start);          // kernel image
    memory_reserve(virt_to_phys((unsigned long)g_fdt_base), g_fdt_size);                  // dtb blob
    memory_reserve(virt_to_phys((unsigned long)g_initrd_base),
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

    // timer：最後才開，避免 heartbeat 在其它 init 期間觸發
    timer_init();

    // QEMU ramfb：fw_cfg 註冊 framebuffer。OrangePi RV2 沒 ramfb，
    // 在那條路 video_init 會 fw_cfg_find_file 找不到無聲 return，安全。
    {
        extern void video_init(void);
        video_init();
    }

    schedule_init();
    kthread_init();
    kthread_bootstrap_idle();

    kthread_create(shell_thread);   // shell：preemption 帶它跑

    start_preempt();

    idle();   /* 不會回來：main thread 變成 idle 跑 kill_zombies + schedule loop */
}
