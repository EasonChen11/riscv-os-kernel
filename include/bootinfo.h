#ifndef BOOTINFO_H
#define BOOTINFO_H

#include <stdint.h>

// 平台/開機資訊：由 bootinfo_init() 在 start_kernel() 中統一填入

extern const void*   g_fdt_base;          // DTB 起始位址
extern unsigned long g_fdt_size;          // DTB 總大小
extern const void*   g_initrd_base;       // initrd 起始位址
extern unsigned long g_initrd_end;        // initrd 結束位址
extern unsigned long g_mem_base;          // /memory reg base
extern unsigned long g_mem_size;          // /memory reg size
extern unsigned long g_kernel_reloc_addr; // kernel 自我搬遷後的位址
extern unsigned long g_kernel_start;      // kernel image 起點（linker symbol _start）
extern unsigned long g_kernel_end;        // kernel image 終點（linker symbol _end）

void bootinfo_init(const void* fdt, unsigned long reloc_addr);
void bootinfo_print(void);

#endif // BOOTINFO_H
