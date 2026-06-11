#include "ramfs.h"
#include "string.h"
#include "dynalloc.h"
#include "cpio.h"
#include "bootinfo.h"

#define RAMFS_MAX_ENTRIES 64

enum ramfs_type { RAMFS_FILE, RAMFS_DIR };

struct ramfs_node {
    char  name[VFS_MAX_COMPONENT];
    enum ramfs_type type;
    const char* content;            /* 指向 cpio 內資料，不複製 */
    uint64_t size;
    struct vnode* vnode;
    struct vnode* entries[RAMFS_MAX_ENTRIES];
    int   n_entries;
};

static struct vnode_operations ramfs_v_ops;
static struct file_operations  ramfs_f_ops;

static struct vnode* ramfs_new_vnode(enum ramfs_type type, const char* name, struct vnode* parent) {
    struct vnode*      v = dynalloc_alloc(sizeof(struct vnode));
    struct ramfs_node* n = dynalloc_alloc(sizeof(struct ramfs_node));
    strlcpy(n->name, name, VFS_MAX_COMPONENT);
    n->type = type;
    n->content = (const char*)0;
    n->size = 0;
    n->vnode = v;
    n->n_entries = 0;
    v->mount = (struct mount*)0;
    v->parent = parent ? parent : v;
    v->v_ops = &ramfs_v_ops;
    v->f_ops = &ramfs_f_ops;
    v->internal = n;
    return v;
}

int ramfs_setup_mount(struct filesystem* fs, struct mount* mount) {
    mount->fs   = fs;
    mount->root = ramfs_new_vnode(RAMFS_DIR, "/", (struct vnode*)0);
    return E_OK;
}

/* 把 initramfs.cpio 的內容填進 ramfs root vnode。
 * 與 setup_mount 分開，方便驗證空 ramfs 與填入後的差異。 */
void ramfs_populate_cpio(struct vnode* root_vnode) {
    struct ramfs_node* root = root_vnode->internal;
    struct cpio_entry_info info;
    const void* rd = g_initrd_base;
    while ((rd = parse_one_entry(rd, &info)) != (void*)0) {
        if (strcmp(info.filename, ".") == 0) continue;
        if (root->n_entries >= RAMFS_MAX_ENTRIES) break;
        struct vnode* v = ramfs_new_vnode(RAMFS_FILE, info.filename, root_vnode);
        struct ramfs_node* n = v->internal;
        n->content = info.filecontent;
        n->size = (uint64_t)info.filesize;
        root->entries[root->n_entries++] = v;
    }
}

/* ---- v_ops（唯讀：create/mkdir 失敗） ---- */
static int ramfs_lookup(struct vnode* dir, struct vnode** target, const char* name) {
    struct ramfs_node* d = dir->internal;
    if (d->type != RAMFS_DIR) return E_NOTDIR;
    for (int i = 0; i < d->n_entries; i++) {
        struct ramfs_node* e = d->entries[i]->internal;
        if (strcmp(e->name, name) == 0) { *target = d->entries[i]; return E_OK; }
    }
    return E_NOENT;
}
static int ramfs_create(struct vnode* d, struct vnode** t, const char* nm) { return E_RDONLY; }
static int ramfs_mkdir (struct vnode* d, struct vnode** t, const char* nm) { return E_RDONLY; }

static int ramfs_readdir(struct vnode* dir, int index, char* name, uint64_t* size) {
    struct ramfs_node* d = dir->internal;
    if (d->type != RAMFS_DIR) return E_NOTDIR;
    if (index < 0 || index >= d->n_entries) return E_NOENT;
    struct ramfs_node* e = d->entries[index]->internal;
    strlcpy(name, e->name, VFS_MAX_COMPONENT);
    if (size) *size = e->size;
    return E_OK;
}

/* ---- f_ops（唯讀：write 失敗） ---- */
static int ramfs_open(struct vnode* file_node, struct file** target) {
    struct file* f = dynalloc_alloc(sizeof(struct file));
    f->vnode = file_node;
    f->f_pos = 0;
    f->f_ops = file_node->f_ops;
    f->flags = 0;
    f->refcount = 1;
    *target = f;
    return E_OK;
}
static int ramfs_close(struct file* file) { dynalloc_free(file); return E_OK; }

static long ramfs_read(struct file* file, void* buf, uint64_t len) {
    struct ramfs_node* n = file->vnode->internal;
    if (n->type != RAMFS_FILE) return E_INVAL;
    uint64_t remain = (file->f_pos < n->size) ? n->size - file->f_pos : 0;
    uint64_t k = (len < remain) ? len : remain;
    if (k) memcpy(buf, n->content + file->f_pos, k);
    file->f_pos += k;
    return (long)k;
}
static long ramfs_write(struct file* f, const void* b, uint64_t n) { return E_RDONLY; }

static long ramfs_lseek64(struct file* f, long off, int w) { return E_INVAL; }
static int  ramfs_ioctl  (struct file* f, unsigned long r, void* a) { return E_INVAL; }

static struct vnode_operations ramfs_v_ops = {
    .lookup  = ramfs_lookup,
    .create  = ramfs_create,
    .mkdir   = ramfs_mkdir,
    .readdir = ramfs_readdir,
};

static struct file_operations ramfs_f_ops = {
    .open    = ramfs_open,
    .close   = ramfs_close,
    .read    = ramfs_read,
    .write   = ramfs_write,
    .lseek64 = ramfs_lseek64,
    .ioctl   = ramfs_ioctl,
};

struct filesystem ramfs = {
    .name = "ramfs",
    .setup_mount = ramfs_setup_mount,
};
