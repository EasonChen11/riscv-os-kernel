#include "exec.h"
#include "uart.h"
#include "string.h"
#include "cpio.h"
#include "bootinfo.h"

/* Load filename from initramfs and jump to it in U-mode via sret. */
void exec_umode(const char *filename) {
    struct cpio_entry_info info;
    const void *rd = g_initrd_base;

    while ((rd = parse_one_entry(rd, &info)) != (void *)0) {
        if (strcmp(info.filename, filename) == 0) {
            unsigned long load_addr = (unsigned long)info.filecontent;
            unsigned long sstatus;
            __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus));
            sstatus &= ~(1UL << 8);  /* SPP = 0  → return to U-mode */
            sstatus |=  (1UL << 5);  /* SPIE = 1 → enable interrupts after sret */
            __asm__ volatile("csrw sscratch, sp");               /* save kernel Stack Pointer in sscratch for later use */
            __asm__ volatile("csrw sstatus, %0" :: "r"(sstatus)); /* set SPP to 0 (U-mode) and SPIE to 1 (enable interrupts after sret) */
            __asm__ volatile("csrw sepc,    %0" :: "r"(load_addr)); /* set entry point to the loaded user program */
            __asm__ volatile("sret"); /* jump to U-mode, execution continues in the loaded user program */
            __builtin_unreachable(); // tell the compiler that this code path will never return
        }
    }
    uart_puts("exec: not found: ");
    uart_puts(filename);
    uart_putc('\n');
}