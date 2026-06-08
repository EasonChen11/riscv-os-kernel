#ifndef TMPFS_H
#define TMPFS_H

#include "vfs.h"

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

extern struct filesystem tmpfs;

int tmpfs_setup_mount(struct filesystem* fs, struct mount* mount);

#endif /* TMPFS_H */
