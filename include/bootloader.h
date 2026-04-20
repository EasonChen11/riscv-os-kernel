#ifndef BOOTLOADER_H
#define BOOTLOADER_H

// Bootloader 邏輯：
// 1. bootloader_find_reloc_addr：給 start.S 在 BSS 清零前呼叫，
//    回傳一個避開 DTB / initrd 的安全自我搬遷位址。
//    ⚠️ 由於在 BSS 清零前呼叫，此函式及其呼叫鏈不可使用任何
//    全域/靜態變數，只能用區域變數。
// 2. bootloader_load_kernel：透過 UART 接收新 kernel image 並跳轉執行。

unsigned long bootloader_find_reloc_addr(const void* fdt, unsigned long kernel_size);
void bootloader_load_kernel(void);

#endif // BOOTLOADER_H
