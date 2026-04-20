#ifndef CPIO_H
#define CPIO_H

#define CPIO_MAGIC 0x070701

#include <stdint.h> // define uintptr_t:把記憶體位址（指標）安全地變成一個數字（整數），讓你能夠對位址進行加減、位元運算，且保證不弄丟任何位元。

struct cpio_entry_info {
    const char* filename;
    const char* filecontent;
    int filesize;
};

struct cpio_t {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
};

const void* parse_one_entry(const void* rd, struct cpio_entry_info* info);
void initrd_list(const void* rd);
void initrd_cat(const void* rd, const char* target);

#endif
