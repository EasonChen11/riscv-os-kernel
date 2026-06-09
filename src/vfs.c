#include "vfs.h"
#include "tmpfs.h"
#include "string.h"
#include "dynalloc.h"
#include "thread.h"

struct mount* rootfs;

/* ---- filesystem 註冊表 ---- */
#define VFS_MAX_FS 8
static struct filesystem* fs_table[VFS_MAX_FS];
static int fs_count;

int register_filesystem(struct filesystem* fs) {
    if (fs_count >= VFS_MAX_FS) return E_INVAL;
    fs_table[fs_count++] = fs;
    return E_OK;
}

struct filesystem* find_filesystem(const char* name) {
    for (int i = 0; i < fs_count; i++)
        if (strcmp(fs_table[i]->name, name) == 0) return fs_table[i];
    return (struct filesystem*)0;
}

/* ---- 裝置註冊表 ---- */
#define VFS_MAX_DEV 8
static struct file_operations* dev_table[VFS_MAX_DEV];
static int dev_count;

int register_dev(struct file_operations* fops) {
    if (dev_count >= VFS_MAX_DEV) return E_INVAL;
    dev_table[dev_count] = fops;
    return dev_count++;          /* 回 device id */
}

void vfs_name_copy(char* dst, const char* src, int dst_size) {
    int i = 0;
    for (; src[i] && i < dst_size - 1; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* ---- 解析起點：一律回 current thread 的 cwd（相對路徑由 path_walk 採用，絕對路徑
 *      則由 path_walk 改用 rootfs root，傳入的 cwd 不會被用到）。boot 期 tp 被 start.S
 *      清 0，kthread_get_current() 回 NULL，null guard 退回 rootfs->root，故開機階段
 *      解析絕對路徑亦安全。 ---- */
static struct vnode* current_cwd(void) {
    struct thread* t = kthread_get_current();
    if (t && t->cwd) return t->cwd;
    return rootfs->root;
}

/* ---- fd table helper ---- */
int fd_alloc(struct thread* t, struct file* f) {
    for (int i = 0; i < THREAD_MAX_FD; i++)
        if (!t->fd_table[i]) { t->fd_table[i] = f; return i; }
    return E_MAXFD;
}

struct file* fd_get(struct thread* t, int fd) {
    if (fd < 0 || fd >= THREAD_MAX_FD) return (struct file*)0;
    return t->fd_table[fd];
}

/* ---- 路徑走訪：支援絕對/相對、連續斜線、"."、".."；跨 mount 於每一步 ---- */
static int path_walk(struct vnode* start, const char* path,
                     struct vnode** out, int stop_at_parent, char* last) {
    struct vnode* cur = (path[0] == '/') ? rootfs->root : start;
    while (cur->mount) cur = cur->mount->root;   /* 起點本身可能是 mount point */

    const char* p = path;
    while (*p) {
        while (*p == '/') p++;                    /* 吃掉連續斜線 */
        if (!*p) break;

        char comp[VFS_MAX_COMPONENT];
        int ci = 0;
        while (*p && *p != '/') {
            if (ci < VFS_MAX_COMPONENT - 1) comp[ci++] = *p;
            p++;
        }
        comp[ci] = '\0';

        const char* q = p;
        while (*q == '/') q++;
        int is_last = (*q == '\0');

        if (is_last && stop_at_parent) {
            vfs_name_copy(last, comp, VFS_MAX_COMPONENT);
            *out = cur;
            return E_OK;
        }
        if (strcmp(comp, ".") == 0 || comp[0] == '\0') continue;
        if (strcmp(comp, "..") == 0) {
            cur = cur->parent;                    /* root 的 parent 指自己 */
            while (cur->mount) cur = cur->mount->root;
            continue;
        }
        struct vnode* next;
        int ret = cur->v_ops->lookup(cur, &next, comp);
        if (ret != E_OK) return ret;
        while (next->mount) next = next->mount->root;   /* 跨進被掛載的 fs */
        cur = next;
    }
    *out = cur;
    return E_OK;
}

int vfs_lookup(const char* pathname, struct vnode** target) {
    return path_walk(current_cwd(), pathname, target, 0, (char*)0);
}

int vfs_lookup_parent(const char* pathname, struct vnode** parent, char* last) {
    return path_walk(current_cwd(), pathname, parent, 1, last);
}

/* ---- 檔案操作 ---- */
int vfs_open(const char* pathname, int flags, struct file** target) {
    struct vnode* node;
    int ret = vfs_lookup(pathname, &node);
    if (ret == E_OK)
        return node->f_ops->open(node, target);

    if (flags & O_CREAT) {
        struct vnode* parent;
        char comp[VFS_MAX_COMPONENT];
        if (vfs_lookup_parent(pathname, &parent, comp) != E_OK) return E_NOENT;
        while (parent->mount) parent = parent->mount->root;
        struct vnode* newv;
        if (parent->v_ops->create(parent, &newv, comp) != E_OK) return E_INVAL;
        return newv->f_ops->open(newv, target);
    }
    return ret;
}

int vfs_close(struct file* file)                          { return file->f_ops->close(file); }
int vfs_read(struct file* file, void* buf, uint64_t len)  { return file->f_ops->read(file, buf, len); }
int vfs_write(struct file* file, const void* buf, uint64_t len){ return file->f_ops->write(file, buf, len); }

/* ---- 目錄 / 掛載 ---- */
int vfs_mkdir(const char* pathname) {
    struct vnode* parent;
    char comp[VFS_MAX_COMPONENT];
    if (vfs_lookup_parent(pathname, &parent, comp) != E_OK) return E_NOENT;
    while (parent->mount) parent = parent->mount->root;
    struct vnode* newv;
    return parent->v_ops->mkdir(parent, &newv, comp);
}

int vfs_mount(const char* target, const char* fsname) {
    struct vnode* mp;
    if (vfs_lookup(target, &mp) != E_OK) return E_NOENT;
    struct filesystem* fs = find_filesystem(fsname);
    if (!fs) return E_INVAL;

    struct mount* mnt = dynalloc_alloc(sizeof(struct mount));
    if (fs->setup_mount(fs, mnt) != E_OK) return E_INVAL;
    /* 讓 mounted root 的 ".." 跨回父 fs：parent = mount point 的父目錄 */
    mnt->root->parent = mp->parent;
    mp->mount = mnt;
    return E_OK;
}

/* ---- mknod：借底層 fs 的 create 取得一個掛在樹上的 vnode，再把 f_ops
 *      覆寫成 driver 的。VFS 不知道它是裝置 → 不 hard-code。 ---- */
int vfs_mknod(const char* path, int dev_id) {
    if (dev_id < 0 || dev_id >= VFS_MAX_DEV || !dev_table[dev_id]) return E_INVAL;
    struct vnode* parent;
    char comp[VFS_MAX_COMPONENT];
    if (vfs_lookup_parent(path, &parent, comp) != E_OK) return E_NOENT;
    while (parent->mount) parent = parent->mount->root;
    struct vnode* v;
    if (parent->v_ops->create(parent, &v, comp) != E_OK) return E_INVAL;
    v->f_ops = dev_table[dev_id];
    return E_OK;
}

/* ---- 開機初始化 ---- */
void vfs_init(void) {
    register_filesystem(&tmpfs);
    rootfs = dynalloc_alloc(sizeof(struct mount));
    tmpfs.setup_mount(&tmpfs, rootfs);
}
