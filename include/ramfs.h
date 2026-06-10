#ifndef RAMFS_H
#define RAMFS_H

#include "vfs.h"

/* ramfs：唯讀 fs，內容鏡射 initramfs.cpio。
 * read 直接從 cpio 原址讀（不複製）；write/create/mkdir 一律失敗。 */
extern struct filesystem ramfs;

void ramfs_populate_cpio(struct vnode* root_vnode);

#endif /* RAMFS_H */
