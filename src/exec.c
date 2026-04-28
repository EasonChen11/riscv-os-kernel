#include "exec.h"
#include "uart.h"
#include "string.h"
#include "cpio.h"
#include "bootinfo.h"
#include "thread.h"
#include "buddy.h"

// 在 initrd 中找檔；找到回 image 起點（cpio entry filecontent），找不到回 NULL。
// exec_umode 跟 do_exec 共用，避免兩條路重寫 cpio walk。
static const void *cpio_lookup(const char *path) {
    struct cpio_entry_info info;
    const void *rd = g_initrd_base;
    while ((rd = parse_one_entry(rd, &info)) != (void *)0) {
        if (strcmp(info.filename, path) == 0)
            return info.filecontent;
    }
    return (void *)0;
}

// kernel thread 第一次跳 U-mode：手動 csrw sscratch/sstatus/sepc 後 sret，一去不回。
// 沒走 trap_frame 路徑（沒 trap 進來過），所以不能像 do_exec 那樣 patch tf。
void exec_umode(const char *path) {
    const void *image = cpio_lookup(path);
    if (!image) {
        uart_puts("exec: not found: ");
        uart_puts(path);
        uart_putc('\n');
        return;
    }
    unsigned long load_addr = (unsigned long)image;
    unsigned long sstatus;
    __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus));
    sstatus &= ~(1UL << 8);  /* SPP = 0  → return to U-mode */
    sstatus |=  (1UL << 5);  /* SPIE = 1 → enable interrupts after sret */
    __asm__ volatile("csrw sscratch, sp");
    __asm__ volatile("csrw sstatus, %0" :: "r"(sstatus));
    __asm__ volatile("csrw sepc,    %0" :: "r"(load_addr));
    __asm__ volatile("sret");
    __builtin_unreachable();
}

// SYS_EXEC：U-mode process 換 user image。沿用當前 thread 的 kstack 與 trap_frame，
// 只改 sepc / sp / sstatus；trap exit 自然 sret 到新 entry，不需手動切 mode。
// ustack lazy-alloc：第一次 exec 才配；後續 exec 重用同一塊（POSIX 語意：exec 不換 process）。
long do_exec(const char *path, struct trap_frame *tf) {
    struct thread *me = kthread_get_current();
    const void *image = cpio_lookup(path);
    if (!image) return -1;
    if (!me->ustack) me->ustack = buddy_alloc_pages(0);
    tf->sepc    = (unsigned long)image;
    tf->sp      = (unsigned long)me->ustack + PAGE_SIZE;
    tf->sstatus &= ~(1UL << 8);   // SPP=0 → 回 U-mode
    tf->sstatus |=  (1UL << 5);   // SPIE=1 → IRQ 在 sret 後恢復
    return 0;
}
