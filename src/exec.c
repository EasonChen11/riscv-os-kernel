#include "exec.h"
#include "uart.h"
#include "string.h"
#include "cpio.h"
#include "bootinfo.h"
#include "thread.h"
#include "buddy.h"
#include "schedule.h"
#include "proc.h"
#include "vm.h"
#include "pgtable.h"
#include "mm.h"
#include "dynalloc.h"
#include "vfs.h"

extern const char sigreturn_trampoline[];
extern const char sigreturn_trampoline_end[];

/* 首個 user process 把 /dev/uart 開成 stdin(0)/stdout(1)/stderr(2)。
 * 只填空槽：fork 來的 child 已繼承、do_exec 來的已保留，不重開。 */
static void setup_std_fds(struct thread *me) {
    for (int i = 0; i < 3; i++) {
        if (me->fd_table[i]) continue;
        struct file *f;
        if (vfs_open("/dev/uart", 0, &f) == E_OK) me->fd_table[i] = f;
    }
}

/* user image @ VA 0x0、user stack top @ 0x4000000000
 * exec 只配/註冊 top 1 page；sp 往下踩出去由 do_page_fault 的 expand-stack 動態擴 vma */
#define USER_STACK_TOP      0x4000000000UL
#define USER_STACK_TOP_PAGE (USER_STACK_TOP - PAGE_SIZE)
#define USER_STACK_BASE_VA  USER_STACK_TOP_PAGE

static int cpio_lookup(const char *path, const void **out_image, int *out_size) {
    struct cpio_entry_info info;
    const void *rd = g_initrd_base;
    while ((rd = parse_one_entry(rd, &info)) != (void *)0) {
        if (strcmp(info.filename, path) == 0) {
            *out_image = info.filecontent;
            *out_size  = info.filesize;
            return 0;
        }
    }
    return -1;
}

/* 登記 code VMA 與 stack VMA；不配任何實體 frame，交給 do_page_fault。
 * code VMA 記住 cpio 來源，供 page fault handler 按頁複製正確內容。 */
static void register_image_vma(struct thread *me, const void *image_src, int image_size) {
    // code VMA：從 VA 0 開始、長度對齊到 page，rwx 權限
    unsigned long img_end = (image_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    struct vma *code_vma = vma_create_at(me, 0x0UL, img_end, PROT_USER_RWX, MAP_ANONYMOUS);
    code_vma->src_va   = image_src;
    code_vma->src_size = image_size;

    // stack VMA：從 USER_STACK_BASE_VA 開始、長度 1 page，rw 權限；初始不 map frame，由 page fault 動態擴張
    me->stack_vma = vma_create_at(me, USER_STACK_BASE_VA, PAGE_SIZE, PROT_USER_RW, MAP_ANONYMOUS);

    // signal trampoline VMA：從任意 VA 開始、長度對齊到 page，rx 權限；初始不 map frame，由 deliver_signal 動態註冊
    unsigned long tramp_size = sigreturn_trampoline_end - sigreturn_trampoline;
    struct vma *sv = vma_alloc(me, PAGE_SIZE, PROT_USER_RX, MAP_ANONYMOUS);
    sv->src_va   = (void *)sigreturn_trampoline;
    sv->src_size = (int)tramp_size;
    me->sigreturn_va = sv->start;
}

/* shell exec：當前 kernel thread 第一次跳 U-mode。手動 csr 配置 + sret。 */
void exec_umode(const char *path) {
    const void *image_src = (void *)0;
    int image_size = 0;
    if (cpio_lookup(path, &image_src, &image_size) < 0) {
        uart_puts("exec: not found: ");
        uart_puts(path);
        uart_putc('\n');
        return;
    }
    struct thread *me = kthread_get_current();

    setup_std_fds(me);   /* fd 0/1/2 → /dev/uart */

    /* 先存舊 PGD 等切換完再 free；否則舊 PGD 那一頁回到 buddy 後
     * 可能被 alloc_pgd / load_user_image 拿去當新 frame 寫壞，但 satp 仍指它，TLB miss 就爛 */
    unsigned long old_pgd = me->pgd_pa;

    me->pgd_pa = alloc_pgd();
    me->is_kernel = false;
    vma_free_all(&me->vma_list);   /* exec 換 image 也丟掉舊 mmap 區段 */
    me->stack_vma = (struct vma *)0;

    register_image_vma(me, image_src, image_size);

    unsigned long kstack_top = (unsigned long)me->kstack + KSTACK_SIZE;

    /* csrw sscratch ~ sret 整段必須 SIE=0；中途 preempt 會踩別 thread 的 kstack */
    __asm__ volatile("csrci sstatus, 2");

    unsigned long sstatus;
    __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus));
    sstatus &= ~(1UL << 8);    /* SPP=0 → U-mode */
    sstatus |=  (1UL << 5);    /* SPIE=1 */

    switch_pgd(me->pgd_pa);    /* kernel half 已 clone，安全 */

    /* satp 已指向新 PGD，舊 PGD 可以安全釋放 */
    if (old_pgd) free_pgd(old_pgd);

    __asm__ volatile("csrw sscratch, %0" :: "r"(kstack_top));
    __asm__ volatile("csrw sstatus,  %0" :: "r"(sstatus));
    __asm__ volatile("csrw sepc,     %0" :: "r"(0x0UL));
    __asm__ volatile("mv   sp,       %0" :: "r"(USER_STACK_TOP));
    __asm__ volatile("sret");
    __builtin_unreachable();
}

static char spawn_path_buf[64];

static void spawn_user_entry(void) {
    exec_umode(spawn_path_buf);
    do_exit(-1);
}

long spawn_user(const char *path) {
    int i;
    for (i = 0; i < (int)sizeof(spawn_path_buf) - 1 && path[i]; i++)
        spawn_path_buf[i] = path[i];
    spawn_path_buf[i] = 0;

    struct thread *parent = kthread_get_current();
    struct thread *child = kthread_create(spawn_user_entry);
    child->ppid = parent->pid;
    list_add_tail(&child->child_link, &parent->children);
    return child->pid;
}

/* SYS_EXEC：U-mode 改 image。沿用當前 thread 的 kstack 與 trap_frame，
 * 重建 PGD、map 新 image / stack，patch tf；trap exit 自然 sret 到新 entry。 */
long do_exec(const char *path, struct trap_frame *tf) {
    struct thread *me = kthread_get_current();
    const void *image_src = (void *)0;
    int image_size = 0;
    if (cpio_lookup(path, &image_src, &image_size) < 0) return -1;

    /* 同 exec_umode：先建新 PGD 再 switch，最後才 free 舊的；
     * 否則舊 PGD 的 frame 一旦被 buddy 重新發出去寫壞，當前 satp 還指它，
     * 任何 TLB miss 都會走到被覆寫的 page table → page fault / hang */
    unsigned long old_pgd = me->pgd_pa;

    me->pgd_pa = alloc_pgd();
    me->is_kernel = false;
    vma_free_all(&me->vma_list);   /* exec 換 image 也丟掉舊 mmap 區段 */
    me->stack_vma = (struct vma *)0;

    register_image_vma(me, image_src, image_size);

    tf->sepc    = 0x0UL;
    tf->sp      = USER_STACK_TOP;
    tf->sstatus &= ~(1UL << 8);   /* SPP=0 */
    tf->sstatus |=  (1UL << 5);   /* SPIE=1 */

    switch_pgd(me->pgd_pa);

    if (old_pgd) free_pgd(old_pgd);
    return 0;
}
