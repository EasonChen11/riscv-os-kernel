/* TODO(lab7): BE1/BE2/BE3 暫時的 in-kernel 自測。
 * 整合 vfs.bin demo 測試後，刪除本檔、vfs.h 的宣告、main.c 的呼叫。 */
#include "vfs.h"
#include "string.h"
#include "uart.h"
#include "thread.h"

static void t_report(const char* name, int pass) {
    uart_puts(pass ? "[PASS] " : "[FAIL] ");
    uart_puts(name);
    uart_puts("\n");
}

void vfs_selftest_be1(void) {
    uart_puts("---- VFS BE1/BE2 selftest ----\n");
    struct file* f;
    char buf[16];

    /* 1) O_CREAT 建檔 + write/read roundtrip */
    int ok = (vfs_open("/abc", O_CREAT, &f) == E_OK);
    ok = ok && (vfs_write(f, "hello", 5) == 5);
    ok = ok && (vfs_close(f) == E_OK);
    memset(buf, 0, sizeof(buf));
    ok = ok && (vfs_open("/abc", 0, &f) == E_OK);
    int k = vfs_read(f, buf, sizeof(buf));
    ok = ok && (k == 5) && (strcmp(buf, "hello") == 0);
    vfs_close(f);
    t_report("create + write/read roundtrip", ok);

    /* 2) 不帶 O_CREAT 開不存在檔 → 失敗 */
    t_report("open missing without O_CREAT fails", vfs_open("/nope", 0, &f) < 0);

    /* 3) mkdir 多層 create */
    ok = (vfs_mkdir("/dir") == E_OK);
    ok = ok && (vfs_open("/dir/file", O_CREAT, &f) == E_OK);
    ok = ok && (vfs_write(f, "xy", 2) == 2);
    vfs_close(f);
    t_report("mkdir + nested create", ok);

    /* 4) "." / ".." / 連續斜線正規化 */
    struct vnode *a, *b;
    ok = (vfs_lookup("/dir", &a) == E_OK);
    ok = ok && (vfs_lookup("/dir/./../dir", &b) == E_OK) && (a == b);
    ok = ok && (vfs_lookup("//dir///file", &b) == E_OK);
    t_report("path normalize . .. //", ok);

    /* 5) mount + 跨 mount 解析 + 跨 mount ".." */
    ok = (vfs_mkdir("/mnt") == E_OK);
    ok = ok && (vfs_mount("/mnt", "tmpfs") == E_OK);
    ok = ok && (vfs_open("/mnt/x", O_CREAT, &f) == E_OK);
    vfs_close(f);
    ok = ok && (vfs_lookup("/mnt/..", &b) == E_OK) && (b == rootfs->root);
    t_report("mount + cross-mount + ..", ok);

    /* 6) EOF：讀超過檔尾回 0 */
    ok = (vfs_open("/abc", 0, &f) == E_OK);
    ok = ok && (vfs_read(f, buf, 16) == 5);   /* 全讀完 */
    ok = ok && (vfs_read(f, buf, 16) == 0);   /* 已在 EOF */
    vfs_close(f);
    t_report("read stops at EOF", ok);

    /* 7) BE4 ramfs：讀 cpio 內檔（osc.txt = 971 bytes，開頭 '\n'），write/create 失敗 */
    ok = (vfs_open("/ramfs/osc.txt", 0, &f) == E_OK);
    int total = 0, kk;
    int first = -1;
    while ((kk = vfs_read(f, buf, sizeof(buf))) > 0) {
        if (first < 0) first = (unsigned char)buf[0];
        total += kk;
    }
    vfs_close(f);
    ok = ok && (total == 971) && (first == '\n');
    ok = ok && (vfs_open("/ramfs/osc.txt", 0, &f) == E_OK);
    ok = ok && (vfs_write(f, "x", 1) < 0);            /* 唯讀，寫失敗 */
    vfs_close(f);
    struct file* rf;
    ok = ok && (vfs_open("/ramfs/newfile", O_CREAT, &rf) < 0);  /* create 失敗 */
    ok = ok && (vfs_mkdir("/ramfs/newdir") < 0);                /* mkdir 失敗 */
    t_report("ramfs read + read-only", ok);

    /* 8) 跨 ramfs mount 的 ".." 回根 fs */
    struct vnode* rv;
    ok = (vfs_lookup("/ramfs/..", &rv) == E_OK) && (rv == rootfs->root);
    t_report("ramfs '..' crosses mount to root", ok);

    uart_puts("---- selftest done ----\n");
}

/* BE3：在 kernel thread context 跑（tp 有效、cwd 已是 rootfs root），
 * 驗證 fd table 重用、相對路徑、chdir、".." 邊界。 */
void vfs_selftest_be3_thread(void) {
    uart_puts("---- VFS BE3 selftest ----\n");
    struct thread* me = kthread_get_current();
    struct file *f0, *f1, *f2;

    /* 1) fd_alloc 回最小空槽、fd_get 取回、close 後槽位可重用 */
    vfs_open("/abc", 0, &f0);
    vfs_open("/abc", 0, &f1);
    vfs_open("/abc", 0, &f2);
    int a = fd_alloc(me, f0), b = fd_alloc(me, f1), c = fd_alloc(me, f2);
    int ok = (a == 0 && b == 1 && c == 2);
    ok = ok && (fd_get(me, 1) == f1);
    vfs_close(me->fd_table[1]); me->fd_table[1] = (struct file*)0;   /* 模擬 close(1) */
    ok = ok && (fd_get(me, 1) == (struct file*)0);
    struct file* f3; vfs_open("/abc", 0, &f3);
    ok = ok && (fd_alloc(me, f3) == 1);     /* 重用最小空槽 1 */
    ok = ok && (fd_get(me, 9) == (struct file*)0 && fd_get(me, -1) == (struct file*)0);
    t_report("fd_table alloc/get/reuse", ok);

    /* 2) 相對路徑 + chdir（直接設 cwd，等同 sys_chdir 內部） */
    vfs_mkdir("/be3");
    vfs_mkdir("/be3/sub");
    struct vnode *v, *be3, *root = rootfs->root;
    ok = (vfs_lookup("/be3", &be3) == E_OK);
    me->cwd = be3;                                  /* chdir("/be3") */
    ok = ok && (vfs_lookup("sub", &v) == E_OK);     /* 相對 */
    ok = ok && (vfs_lookup("./sub", &v) == E_OK);
    ok = ok && (vfs_lookup("..", &v) == E_OK) && (v == root);       /* /be3 的 parent = root */
    ok = ok && (vfs_lookup("sub/..", &v) == E_OK) && (v == be3);
    t_report("relative path + chdir", ok);

    /* 3) ".." 在 root 的邊界：root 的 parent 仍是 root */
    me->cwd = root;
    ok = (vfs_lookup("..", &v) == E_OK) && (v == root);
    ok = ok && (vfs_lookup("/..", &v) == E_OK) && (v == root);
    t_report("'..' at root stays root", ok);

    me->cwd = root;   /* 還原，避免影響後續 */
    uart_puts("---- BE3 selftest done ----\n");
    thread_exit();
}
