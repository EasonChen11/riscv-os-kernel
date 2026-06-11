#include "vfs.h"
#include "dev.h"
#include "dynalloc.h"
#include "uart.h"

/* /dev/uart：special file，R/W 等同 uartread / uartwrite。f_pos 不推進。 */

static int uart_dev_open(struct vnode* node, struct file** target) {
    struct file* f = dynalloc_alloc(sizeof(struct file));
    f->vnode = node;
    f->f_pos = 0;
    f->f_ops = node->f_ops;
    f->flags = 0;
    f->refcount = 1;
    *target = f;
    return E_OK;
}

static int uart_dev_close(struct file* f) {
    dynalloc_free(f);
    return E_OK;
}

static long uart_dev_read(struct file* f, void* buf, uint64_t len) {
    char* b = buf;
    for (uint64_t i = 0; i < len; i++) b[i] = uart_getc();   /* 阻塞讀 */
    return (long)len;
}

static long uart_dev_write(struct file* f, const void* buf, uint64_t len) {
    const char* b = buf;
    for (uint64_t i = 0; i < len; i++) uart_putc(b[i]);
    return (long)len;
}

static struct file_operations uart_dev_fops = {
    .open  = uart_dev_open,
    .close = uart_dev_close,
    .read  = uart_dev_read,
    .write = uart_dev_write,
    .lseek64 = (void*)0,
    .ioctl   = (void*)0,
};

int uart_dev_init(void) {
    return register_dev(&uart_dev_fops);
}
