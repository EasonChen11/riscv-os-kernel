#include "cpio.h"
#include "string.h"
#include "uart.h"

static int hextoi(const char* s, int n) {
    int r = 0;
    while (n-- > 0) {
        r = r << 4;
        if (*s >= 'A')
            r += *s++ - 'A' + 10;
        else if (*s >= 0)
            r += *s++ - '0';
    }
    return r;
}

static inline const void* align_up(const void* ptr, unsigned long align) {
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
} //ptr + align 1 強制對齊到下一個 align 的倍數，& ~(align - 1) 是將低 align 位清零，確保對齊

const void* parse_one_entry(const void* rd, struct cpio_entry_info* info) {
    const struct cpio_t* entry = (const struct cpio_t*)rd;
    int magic = hextoi(entry->magic, 6);
    if (magic != CPIO_MAGIC) {
        uart_puts("Invalid magic number\n");
        return (void*)0;
    }
    int namesize = hextoi(entry->namesize, 8);
    int filesize = hextoi(entry->filesize, 8);
    const char* filename = (const char*)(entry + 1); // filename is right after the cpio header, +1 is sizeof(struct cpio_t)
    const char* filecontent = (const char*)(align_up(filename + namesize, 4)); // file content is after filename, aligned to 4 bytes
    const void* next_rd = align_up(filecontent + filesize, 4); // next entry is after file content, aligned to 4 bytes

    info->filename = filename;
    info->filecontent = filecontent;
    info->filesize = filesize;

    if (strcmp(filename, "TRAILER!!!") == 0)
        return (void*)0;
    return next_rd;
}

void initrd_list(const void* rd) {
    struct cpio_entry_info info;
    const void* counter = rd;
    int file_count = 0;
    while ((counter = parse_one_entry(counter, &info)) != (void*)0)
        file_count++;
    uart_puts("Total ");
    uart_int(file_count);
    uart_puts(" files:\n");
    // 重新從頭開始列出檔案資訊
    while ((rd = parse_one_entry(rd, &info)) != (void*)0) {
        uart_int(info.filesize);
        uart_putc('\t');
        uart_puts(info.filename);
        uart_putc('\n');
    }
    uart_putc('\n');
}

void initrd_cat(const void* rd, const char* target) {
    struct cpio_entry_info info;
    int found = 0;
    while ((rd = parse_one_entry(rd, &info)) != (void*)0) {
        if (strcmp(info.filename, target) == 0) {
            found = 1;
            for (int i = 0; i < info.filesize; i++) {
                uart_putc(info.filecontent[i]);
            }
            uart_putc('\n');
            return;
        }
    }
    if (!found) {
        uart_puts("File not found\n");
    }
}
