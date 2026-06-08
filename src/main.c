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
#include "vm.h"
#include "task.h"
#include "schedule.h"
#include "exec.h"
#include "thread.h"
#include "pgtable.h"
#include "vfs.h"
#include "ramfs.h"
#include "dev.h"
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
    /* Phase 0: 純 FDT 解析 + 純記憶體操作，全程不可寫 MMIO（PTE 尚未建）。
     * bootinfo_init 在 error 路徑有 uart_puts，但 QEMU/U-Boot 的 FDT 合法不會走。 */
    uart_init(fdt);                // 解 /soc/serial(uart)；填 uart_pa/uart_size
    plic_init(fdt, hart_id);       // 解 /soc/plic；填 plic_pa/plic_size、算 plic_ctx
    bootinfo_init(fdt, reloc_addr);

    /* For intterupt:
    task_init 必須先於 trap_init：do_trap 結尾會呼叫 drain_tasks，
    kernel 自我搬遷使 .data 靜態 self-pointing 失效，必須 runtime init_list_head。 */
    task_init();              // init_list_head(&task_list) + task_init 的其他初始化
    trap_init();              // 開 sstatus.SIE（仍無 IRQ source enabled）

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
    // 先截到 linear map 範圍內，超出的尾段 buddy 配出來會無法 phys_to_virt。
    g_mem_size = vm_clamp_ram_size(g_mem_base, g_mem_size);
    buddy_register_range(g_mem_base, g_mem_size);

    // Phase 3: 依 total_pages 精算 heap size（frame_array + page_descriptors + slack），掃 reserved 列表找首塊可用空洞初始化
    unsigned long heap_size = sizeof(struct frame) * g_buddy_total_pages
                            + dynalloc_metadata_bytes(g_buddy_total_pages)
                            + STARTUP_HEAP_SLACK;
    startup_init(heap_size);

    // Phase 4: 交棒 buddy / dynalloc，它們會從 startup heap 動態拿 frame_array / page_descriptors
    buddy_init();
    dynalloc_init();

    /* Phase 5: MMIO 4 KiB PTE 上線（lab6.rst Finer Granularity Paging）。
     * 必須 buddy_init 之後（map_pages 需要 buddy 配 PMD/PTE table），
     * plic_enable 之前（PLIC 第一次 MMIO 寫），不需要 irq_save 因為 IRQ source 還沒 enable。 */
    mmio_finalize(uart_pa, uart_size, plic_pa, plic_size);

    plic_enable();            // PLIC MMIO write threshold + sie.SEIE
    uart_irq_init();          // 設 UART IER/MCR/FCR + PLIC enable；interrupt 路徑就緒

    // 從這裡開始 uart_puts 可用（interrupt 模式下由 UART ISR drain TX ring）
    uart_init_print_info();

    // VFS：register tmpfs + 建 rootfs（只用 dynalloc，buddy 已就緒供寫檔配頁）
    vfs_init();
    // BE4：ramfs 唯讀掛在 /ramfs，內容鏡射 initramfs.cpio
    register_filesystem(&ramfs);
    vfs_mkdir("/ramfs");
    vfs_mount("/ramfs", "ramfs");
    // Adv1：UART special file 掛在 /dev/uart（fd 0/1/2 由 exec_umode 開）
    int uart_id = uart_dev_init();
    vfs_mkdir("/dev");
    vfs_mknod("/dev/uart", uart_id);
    // Adv2：framebuffer special file 掛在 /dev/fb。mknod 只註冊裝置檔不碰 FB；
    // 真正寫 FB_BASE_VA 在 user 跑 write(/dev/fb) 時，那時下方 video_init 早已跑完。
    int fb_id = fb_dev_init();
    vfs_mknod("/dev/fb", fb_id);

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
