#include "exec.h"
#include "uart.h"
#include "string.h"
#include "cpio.h"
#include "bootinfo.h"
#include "thread.h"
#include "buddy.h"
#include "schedule.h"
#include "proc.h"

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
//
// 重要：必須切 user 的 sp 跟 sscratch 到不同 stack，**不能沿用當下 kernel sp**：
//   - user sp 設 ustack top：user 在 ustack 內 push frame，trap 進來不會撞
//   - sscratch 設 kstack top：trap.S push trap_frame 在 kstack 內，跟 ustack 隔離
//   兩條 stack 必須在不同 page，否則 trap_frame 跟 user stack 重疊（發現的 BUG）
void exec_umode(const char *path) {
    const void *image = cpio_lookup(path);
    if (!image) {
        uart_puts("exec: not found: ");
        uart_puts(path);
        uart_putc('\n');
        return;
    }
    struct thread *me = kthread_get_current();
    if (!me->ustack) me->ustack = buddy_alloc_pages(0);
    me->is_kernel = false;   // 從這條 thread 切到 user image，stop/kill 該擋的保護解除
    unsigned long ustack_top = (unsigned long)me->ustack + PAGE_SIZE;
    unsigned long kstack_top = (unsigned long)me->kstack + KSTACK_SIZE;
    unsigned long load_addr  = (unsigned long)image;

    // 從 csrw sscratch 到 sret 之間若被 preempt 打斷，trap.S 會把當下
    // sscratch 換出、schedule 切到別 thread；switch_to 不存 sscratch，
    // 切回來繼續跑 sret 後 sscratch 是別人的 kstack_top，user 第一次 ecall
    // 就會把 trap_frame 寫到別人的 kstack 上。所以整段必須 SIE=0。
    // sret 後 SIE 從 SPIE 還原，user 跑起來中斷一樣正常。
    __asm__ volatile("csrci sstatus, 2");

    unsigned long sstatus;
    __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus));
    sstatus &= ~(1UL << 8);  /* SPP = 0  → return to U-mode */
    sstatus |=  (1UL << 5);  /* SPIE = 1 → IRQ 在 sret 後恢復 */

    __asm__ volatile("csrw sscratch, %0" :: "r"(kstack_top));   // ★ kstack top，不是當下 sp
    __asm__ volatile("csrw sstatus,  %0" :: "r"(sstatus));
    __asm__ volatile("csrw sepc,     %0" :: "r"(load_addr));
    __asm__ volatile("mv   sp,       %0" :: "r"(ustack_top));    // ★ 切 user sp 到 ustack
    __asm__ volatile("sret");
    __builtin_unreachable();
}

// shell 用：開新 thread 跑 exec_umode(path)，當前 thread 為 parent。
// 一次只支援一個 in-flight spawn（path buffer 是靜態的）；實作 fork+exec 之後可以拆掉。
static char spawn_path_buf[64];

static void spawn_user_entry(void) {
    exec_umode(spawn_path_buf);    // 成功不回；失敗印過字
    do_exit(-1);                   // exec failed → 標 ZOMBIE，parent waitpid 會醒
}

// only from shell call exec_cmd("exec ...")，不直接暴露 sys_exec syscall 給 user 程式；實作 fork+exec 後可以改成直接 sys_exec。
long spawn_user(const char *path) {
    int i;
    for (i = 0; i < (int)sizeof(spawn_path_buf) - 1 && path[i]; i++)
        spawn_path_buf[i] = path[i];
    spawn_path_buf[i] = 0;

    struct thread *parent = kthread_get_current(); // shell
    struct thread *c = kthread_create(spawn_user_entry); // new thread starts at spawn_user_entry
    c->ppid = parent->pid;
    list_add_tail(&c->child_link, &parent->children);
    return c->pid;
}

// SYS_EXEC：U-mode process 換 user image。沿用當前 thread 的 kstack 與 trap_frame，
// 只改 sepc / sp / sstatus；trap exit 自然 sret 到新 entry，不需手動切 mode。
// ustack lazy-alloc：第一次 exec 才配；後續 exec 重用同一塊（POSIX 語意：exec 不換 process）。
long do_exec(const char *path, struct trap_frame *tf) {
    struct thread *me = kthread_get_current();
    const void *image = cpio_lookup(path);
    if (!image) return -1;
    if (!me->ustack) me->ustack = buddy_alloc_pages(0);
    me->is_kernel = false;   // 已是 user (能呼叫 SYS_EXEC 表示原本就是 user)；保險明確標一次
    tf->sepc    = (unsigned long)image;
    tf->sp      = (unsigned long)me->ustack + PAGE_SIZE;
    tf->sstatus &= ~(1UL << 8);   // SPP=0 → 回 U-mode
    tf->sstatus |=  (1UL << 5);   // SPIE=1 → IRQ 在 sret 後恢復
    return 0;
}
