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

/* lab6 rst：user image @ VA 0x0、user stack top @ 0x4000000000、stack 1 page */
#define USER_STACK_TOP      0x4000000000UL
#define USER_STACK_BASE_VA  0x3ffffff000UL

static void vma_add_simple(struct list_head *head, unsigned long start,
                           unsigned long end, unsigned long pte_prot) {
    struct vma *v = dynalloc_alloc(sizeof(*v));
    v->start    = start;
    v->end      = end;
    v->pte_prot = pte_prot;
    v->flags    = MAP_ANONYMOUS | MAP_POPULATE;
    init_list_head(&v->link);
    list_add_tail(&v->link, head);   /* head 為空時等同插在最前 */
}

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

/* 把 user image 一頁一頁配新 frame + memcpy + map 進 user PGD VA 0x0 起 */
static void load_user_image(unsigned long pgd_pa, const void *image_src, int size)
{
    int npages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (int i = 0; i < npages; i++) {
        void *p = buddy_alloc_pages(0);
        memset(p, 0, PAGE_SIZE);
        int n = (i == npages - 1) ? (size - i * PAGE_SIZE) : PAGE_SIZE;
        memcpy(p, (const char *)image_src + i * PAGE_SIZE, n);
        map_pages(pgd_pa, (unsigned long)i * PAGE_SIZE, PAGE_SIZE,
                  virt_to_phys((unsigned long)p), PROT_USER_RWX);
    }
}

static void map_user_stack(unsigned long pgd_pa)
{
    void *s = buddy_alloc_pages(0);
    memset(s, 0, PAGE_SIZE);
    map_pages(pgd_pa, USER_STACK_BASE_VA, PAGE_SIZE,
              virt_to_phys((unsigned long)s), PROT_USER_RW);
}

/* 把 image 與 stack 登記進 vma_list，讓 mmap 的 overlap 判斷看得到。
 * 區段 round-up 到 page 邊界。 */
static void register_image_vma(struct thread *me, int image_size) {
    unsigned long img_end = (image_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    vma_add_simple(&me->vma_list, 0x0UL, img_end, PROT_USER_RWX);
    vma_add_simple(&me->vma_list, USER_STACK_BASE_VA, USER_STACK_TOP, PROT_USER_RW);
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

    if (me->pgd_pa) free_pgd(me->pgd_pa);
    me->pgd_pa = alloc_pgd();
    me->is_kernel = false;
    vma_free_all(&me->vma_list);   /* exec 換 image 也丟掉舊 mmap 區段 */

    load_user_image(me->pgd_pa, image_src, image_size);
    map_user_stack(me->pgd_pa);
    register_image_vma(me, image_size);

    unsigned long kstack_top = (unsigned long)me->kstack + KSTACK_SIZE;

    /* csrw sscratch ~ sret 整段必須 SIE=0；中途 preempt 會踩別 thread 的 kstack */
    __asm__ volatile("csrci sstatus, 2");

    unsigned long sstatus;
    __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus));
    sstatus &= ~(1UL << 8);    /* SPP=0 → U-mode */
    sstatus |=  (1UL << 5);    /* SPIE=1 */

    switch_pgd(me->pgd_pa);    /* kernel half 已 clone，安全 */

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
    struct thread *c = kthread_create(spawn_user_entry);
    c->ppid = parent->pid;
    list_add_tail(&c->child_link, &parent->children);
    return c->pid;
}

/* SYS_EXEC：U-mode 改 image。沿用當前 thread 的 kstack 與 trap_frame，
 * 重建 PGD、map 新 image / stack，patch tf；trap exit 自然 sret 到新 entry。 */
long do_exec(const char *path, struct trap_frame *tf) {
    struct thread *me = kthread_get_current();
    const void *image_src = (void *)0;
    int image_size = 0;
    if (cpio_lookup(path, &image_src, &image_size) < 0) return -1;

    if (me->pgd_pa) free_pgd(me->pgd_pa);
    me->pgd_pa = alloc_pgd();
    me->is_kernel = false;
    vma_free_all(&me->vma_list);   /* exec 換 image 也丟掉舊 mmap 區段 */

    load_user_image(me->pgd_pa, image_src, image_size);
    map_user_stack(me->pgd_pa);
    register_image_vma(me, image_size);

    tf->sepc    = 0x0UL;
    tf->sp      = USER_STACK_TOP;
    tf->sstatus &= ~(1UL << 8);   /* SPP=0 */
    tf->sstatus |=  (1UL << 5);   /* SPIE=1 */

    switch_pgd(me->pgd_pa);
    return 0;
}
