/* TODO(lab7): BE1/BE2 暫時的 in-kernel 自測。
 * 整合 vfs.bin demo 測試後，刪除本檔、vfs.h 的宣告、main.c 的呼叫。 */
#include "vfs.h"
#include "string.h"
#include "uart.h"

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

    uart_puts("---- selftest done ----\n");
}
