#include "tmpfs.h"
#include "string.h"
#include "dynalloc.h"
#include "buddy.h"

#define TMPFS_MAX_ENTRIES 16    /* 一個目錄最多 16 個 entry */
#define TMPFS_MAX_FILESIZE 4096 /* 單檔最大 4096 byte */

enum tmpfs_type { TMPFS_DIR, TMPFS_FILE };

struct tmpfs_node {
    char  name[VFS_MAX_COMPONENT];
    enum  tmpfs_type type;
    struct vnode* vnode;
    /* DIR */
    struct vnode* entries[TMPFS_MAX_ENTRIES];
    int   n_entries;
    /* FILE */
    char* data;                 /* 第一次需要時用 buddy 配一頁（4096）；dynalloc 上限 2048 放不下 */
    uint64_t size;
};

static struct vnode_operations tmpfs_v_ops;
static struct file_operations  tmpfs_f_ops;

/* 建一個 tmpfs vnode；parent 為 NULL 時自指（給 root 用） */
static struct vnode* tmpfs_new_vnode(enum tmpfs_type type, const char* name,
                                     struct vnode* parent) {
    struct vnode*      v = dynalloc_alloc(sizeof(struct vnode));
    struct tmpfs_node* n = dynalloc_alloc(sizeof(struct tmpfs_node));
    strlcpy(n->name, name, VFS_MAX_COMPONENT);
    n->type = type;
    n->vnode = v;
    n->n_entries = 0;
    n->data = (char*)0;
    n->size = 0;
    v->mount = (struct mount*)0;
    v->parent = parent ? parent : v;
    v->v_ops = &tmpfs_v_ops;
    v->f_ops = &tmpfs_f_ops;
    v->internal = n;
    return v;
}

int tmpfs_setup_mount(struct filesystem* fs, struct mount* mount) {
    mount->fs = fs;
    mount->root = tmpfs_new_vnode(TMPFS_DIR, "/", (struct vnode*)0);
    return E_OK;
}

/* ---- v_ops ---- */
static int tmpfs_lookup(struct vnode* dir, struct vnode** target, const char* name) {
    struct tmpfs_node* d = dir->internal;
    if (d->type != TMPFS_DIR) return E_NOTDIR;
    for (int i = 0; i < d->n_entries; i++) {
        struct tmpfs_node* e = d->entries[i]->internal;
        if (strcmp(e->name, name) == 0) { *target = d->entries[i]; return E_OK; }
    }
    return E_NOENT;
}

static struct vnode* tmpfs_add_child(struct vnode* dir, enum tmpfs_type type,
                                     const char* name) {
    struct tmpfs_node* d = dir->internal;
    struct vnode* v = tmpfs_new_vnode(type, name, dir);
    d->entries[d->n_entries++] = v;
    return v;
}

static int tmpfs_create(struct vnode* dir, struct vnode** target, const char* name) {
    struct tmpfs_node* d = dir->internal;
    if (d->type != TMPFS_DIR) return E_NOTDIR;
    if (d->n_entries >= TMPFS_MAX_ENTRIES) return E_INVAL;
    struct vnode* dummy;
    if (tmpfs_lookup(dir, &dummy, name) == E_OK) return E_EXIST;
    *target = tmpfs_add_child(dir, TMPFS_FILE, name);
    return E_OK;
}

static int tmpfs_mkdir(struct vnode* dir, struct vnode** target, const char* name) {
    struct tmpfs_node* d = dir->internal;
    if (d->type != TMPFS_DIR) return E_NOTDIR;
    if (d->n_entries >= TMPFS_MAX_ENTRIES) return E_INVAL;
    struct vnode* dummy;
    if (tmpfs_lookup(dir, &dummy, name) == E_OK) return E_EXIST;
    *target = tmpfs_add_child(dir, TMPFS_DIR, name);
    return E_OK;
}

static int tmpfs_readdir(struct vnode* dir, int index, char* name, uint64_t* size) {
    struct tmpfs_node* d = dir->internal;
    if (d->type != TMPFS_DIR) return E_NOTDIR;
    if (index < 0 || index >= d->n_entries) return E_NOENT;
    struct tmpfs_node* e = d->entries[index]->internal;
    strlcpy(name, e->name, VFS_MAX_COMPONENT);
    if (size) *size = e->size;
    return E_OK;
}

/* ---- f_ops ---- */
static int tmpfs_open(struct vnode* file_node, struct file** target) {
    struct file* f = dynalloc_alloc(sizeof(struct file));
    f->vnode = file_node;
    f->f_pos = 0;
    f->f_ops = file_node->f_ops;
    f->flags = 0;
    f->refcount = 1;
    *target = f;
    return E_OK;
}

static int tmpfs_close(struct file* file) {
    dynalloc_free(file);
    return E_OK;
}

static long tmpfs_read(struct file* file, void* buf, uint64_t len) {
    struct tmpfs_node* n = file->vnode->internal;
    if (n->type != TMPFS_FILE) return E_INVAL;
    uint64_t remain = (file->f_pos < n->size) ? n->size - file->f_pos : 0;
    uint64_t k = (len < remain) ? len : remain;     /* min(len, 可讀)；到 EOF 停 */
    if (k && n->data) memcpy(buf, n->data + file->f_pos, k);
    file->f_pos += k;
    return (long)k;
}

static long tmpfs_write(struct file* file, const void* buf, uint64_t len) {
    struct tmpfs_node* n = file->vnode->internal;
    if (n->type != TMPFS_FILE) return E_INVAL;
    if (!n->data) {                                 /* 第一次寫才配資料頁 */
        n->data = buddy_alloc_pages(0);             /* 4096 byte */
        memset(n->data, 0, TMPFS_MAX_FILESIZE);
    }
    uint64_t space = (file->f_pos < TMPFS_MAX_FILESIZE) ? TMPFS_MAX_FILESIZE - file->f_pos : 0;
    uint64_t k = (len < space) ? len : space;       /* 封頂 4096 */
    if (k) memcpy(n->data + file->f_pos, buf, k);
    file->f_pos += k;
    if (file->f_pos > n->size) n->size = file->f_pos;
    return (long)k;
}

static struct vnode_operations tmpfs_v_ops = {
    .lookup  = tmpfs_lookup,
    .create  = tmpfs_create,
    .mkdir   = tmpfs_mkdir,
    .readdir = tmpfs_readdir,
};

static struct file_operations tmpfs_f_ops = {
    .open  = tmpfs_open,
    .close = tmpfs_close,
    .read  = tmpfs_read,
    .write = tmpfs_write,
    .lseek64 = (void*)0,
    .ioctl   = (void*)0,
};

struct filesystem tmpfs = {
    .name = "tmpfs",
    .setup_mount = tmpfs_setup_mount,
};
