#ifndef VFS_H
#define VFS_H

#include <stdint.h>

/* 開檔旗標 / lseek / ioctl 請求碼（規格固定值） */
#define O_CREAT 00000100
#define SEEK_SET 0
#define FB_IOCTL_GET_INFO 0

/* 規格上限 */
#define VFS_MAX_PATH      255   /* pathname 最長 */
#define VFS_MAX_COMPONENT 16    /* component name <=15 + '\0' */
#define VFS_MAX_FD        16    /* 每 process 開檔數，fd < 16 */

/* 錯誤碼：失敗一律回負值 */
#define E_OK       0
#define E_NOENT   -1   /* 找不到 */
#define E_EXIST   -2   /* create/mkdir 目標已存在 */
#define E_NOTDIR  -3   /* 對非目錄做 lookup/mkdir */
#define E_RDONLY  -4   /* 對唯讀 fs 寫 */
#define E_INVAL   -5   /* 參數錯（壞 fd / 壞 path / 滿） */
#define E_MAXFD   -6   /* fd table 滿 */

struct vnode;
struct file;
struct mount;
struct filesystem;

struct file_operations {
    int  (*open) (struct vnode* file_node, struct file** target);
    int  (*close)(struct file* file);
    long (*read) (struct file* file, void* buf, uint64_t len);
    long (*write)(struct file* file, const void* buf, uint64_t len);
    long (*lseek64)(struct file* file, long offset, int whence);     /* Adv2 */
    int  (*ioctl)(struct file* file, unsigned long request, void* arg); /* Adv2 */
};

struct vnode_operations {
    int (*lookup)(struct vnode* dir, struct vnode** target, const char* component_name);
    int (*create)(struct vnode* dir, struct vnode** target, const char* component_name);
    int (*mkdir) (struct vnode* dir, struct vnode** target, const char* component_name);
    /* 列出 dir 的第 index 筆 child：填 name/size，回 E_OK；index 越界回 E_NOENT 表結束 */
    int (*readdir)(struct vnode* dir, int index, char* name, uint64_t* size);
};

struct vnode {
    struct mount* mount;             /* 非 NULL：此 vnode 被掛載，解析時降到 mount->root */
    struct vnode* parent;            /* 父目錄 vnode（給 ".."）；root 指自己 */
    struct vnode_operations* v_ops;
    struct file_operations*  f_ops;
    void* internal;                  /* 底層 fs 的私有節點 */
};

struct file {
    struct vnode* vnode;
    uint64_t f_pos;                  /* 下一次 R/W 位置（special file 可不推進） */
    struct file_operations* f_ops;
    int flags;
};

struct mount {
    struct vnode* root;
    struct filesystem* fs;
};

struct filesystem {
    const char* name;
    int (*setup_mount)(struct filesystem* fs, struct mount* mount);
};

struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int bpp;                /* byte per pixel */
};

extern struct mount* rootfs;

/* fs / 裝置註冊 */
int register_filesystem(struct filesystem* fs);
struct filesystem* find_filesystem(const char* name);

/* 裝置註冊（Adv1 起）：driver 註冊 file_operations 拿到 dev id；
 * vfs_mknod 在 fs 建一個 special file，把 f_ops 覆寫成 driver 的。 */
int register_dev(struct file_operations* fops);
int vfs_mknod(const char* path, int dev_id);

/* 開機初始化：register tmpfs + 建 rootfs */
void vfs_init(void);

/* 若 vnode 是 mount point，降到被掛載 fs 的 root（可能多層） */
struct vnode* vnode_follow_mount(struct vnode* v);

/* 路徑解析 */
int vfs_lookup(const char* pathname, struct vnode** target);
int vfs_lookup_parent(const char* pathname, struct vnode** parent, char* last);

/* 目錄列舉：解析 pathname 為目錄 vnode，取第 index 筆 child 的 name/size */
int vfs_readdir(const char* pathname, int index, char* name, uint64_t* size);

/* 檔案操作（VFS 層） */
int vfs_open(const char* pathname, int flags, struct file** target);
int vfs_close(struct file* file);
long vfs_read(struct file* file, void* buf, uint64_t len);
long vfs_write(struct file* file, const void* buf, uint64_t len);

/* 目錄 / 掛載（BE2 起） */
int vfs_mkdir(const char* pathname);
int vfs_mount(const char* target, const char* fsname);

/* file descriptor table helper（BE3 起） */
struct thread;
int fd_alloc(struct thread* t, struct file* f);   /* 回最小空槽 index，滿則 E_MAXFD */
struct file* fd_get(struct thread* t, int fd);    /* 壞 fd / 未開 回 NULL */

#endif /* VFS_H */
