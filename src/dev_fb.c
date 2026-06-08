#include "vfs.h"
#include "dev.h"
#include "dynalloc.h"
#include "video.h"
#include "string.h"

/* /dev/fb：write-only special file。寫到 framebuffer lfb + f_pos，flush D-cache。
 * lseek64(SEEK_SET) 重定位 f_pos（免重開檔再畫下一格）；
 * ioctl(FB_IOCTL_GET_INFO) 回 framebuffer_info。
 * QEMU 的 framebuffer 已在開機 video_init 透過 fw_cfg 設好，driver 直接用 FB_BASE_VA。 */

static int fb_open(struct vnode* node, struct file** target) {
    struct file* f = dynalloc_alloc(sizeof(struct file));
    f->vnode = node;
    f->f_pos = 0;
    f->f_ops = node->f_ops;
    f->flags = 0;
    *target = f;
    return E_OK;
}

static int fb_close(struct file* f) {
    dynalloc_free(f);
    return E_OK;
}

static int fb_read(struct file* f, void* buf, uint64_t len) {
    return E_INVAL;        /* write-only */
}

static int fb_write(struct file* f, const void* buf, uint64_t len) {
    void* dst = (void*)(FB_BASE_VA + f->f_pos);
    memcpy(dst, buf, len);
    flush_dcache(dst, len);
    f->f_pos += len;       /* 推進，連續 write 接著畫 */
    return (int)len;
}

static long fb_lseek64(struct file* f, long offset, int whence) {
    if (whence == SEEK_SET) { f->f_pos = (uint64_t)offset; return (long)f->f_pos; }
    return E_INVAL;
}

static int fb_ioctl(struct file* f, unsigned long request, void* arg) {
    if (request != FB_IOCTL_GET_INFO) return E_INVAL;
    struct framebuffer_info* out = arg;   /* user 指標：syscall 期間 satp 仍 user PGD，直寫 */
    out->width  = FB_WIDTH;
    out->height = FB_HEIGHT;
    out->bpp    = FB_BPP;
    return E_OK;
}

static struct file_operations fb_dev_fops = {
    .open    = fb_open,
    .close   = fb_close,
    .read    = fb_read,
    .write   = fb_write,
    .lseek64 = fb_lseek64,
    .ioctl   = fb_ioctl,
};

int fb_dev_init(void) {
    return register_dev(&fb_dev_fops);
}
