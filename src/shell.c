#include "shell.h"
#include "uart.h"
#include "cpio.h"
#include "string.h"
#include "bootinfo.h"
#include "bootloader.h"
#include "buddy.h"
#include "dynalloc.h"

static char cmd_buffer[MAX_CMD_LEN];
static int cmd_ptr = 0;

void shell_init(void) {
}

// ─── shell command handlers ────────────────────────────────────
static void cmd_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help  - show all commands.\n");
    uart_puts("  hello - print Hello, World.\n");
    uart_puts("  info  - print system info.\n");
    uart_puts("  ls    - list initrd files.\n");
    uart_puts("  cat   - print file content. usage: cat <filename>\n");
    uart_puts("  load  - load kernel image via UART.\n");
    uart_puts("  mmtest - run buddy allocator test.\n");
}

// 對照 lab3.rst test_alloc_1：透過 dynalloc 統一 API 測試 buddy + chunk pool。
static void cmd_mmtest(void) {
    buddy_reset_step();
    uart_puts("Testing buddy allocator (large blocks via dynalloc)...\n");

    // 4000 / 8000 bytes 走 dynalloc 大塊路徑 → buddy_alloc_pages
    char *ptr1 = (char *)dynalloc_alloc(4000);
    char *ptr2 = (char *)dynalloc_alloc(8000);
    char *ptr3 = (char *)dynalloc_alloc(4000);
    char *ptr4 = (char *)dynalloc_alloc(4000);

    dynalloc_free(ptr1);
    dynalloc_free(ptr2);
    dynalloc_free(ptr3);
    dynalloc_free(ptr4);

    uart_puts("Testing dynamic allocator (chunk pools)...\n");
    char *kmem_ptr1 = (char *)dynalloc_alloc(16);
    char *kmem_ptr2 = (char *)dynalloc_alloc(32);
    char *kmem_ptr3 = (char *)dynalloc_alloc(64);
    char *kmem_ptr4 = (char *)dynalloc_alloc(128);

    dynalloc_free(kmem_ptr1);
    dynalloc_free(kmem_ptr2);
    dynalloc_free(kmem_ptr3);
    dynalloc_free(kmem_ptr4);

    char *kmem_ptr5 = (char *)dynalloc_alloc(16);
    char *kmem_ptr6 = (char *)dynalloc_alloc(32);
    dynalloc_free(kmem_ptr5);
    dynalloc_free(kmem_ptr6);

    // 測試 chunk cache 不足時自動向 buddy 要新 page
    void *kmem_ptr[100];
    for (int i = 0; i < 100; i++) {
        kmem_ptr[i] = (char *)dynalloc_alloc(128);
    }
    for (int i = 0; i < 100; i++) {
        dynalloc_free(kmem_ptr[i]);
    }

    // 測試超過最大可配大小
    unsigned int over_max = (1U << BUDDY_MAX_ORDER) * PAGE_SIZE + 1;
    char *kmem_ptr7 = (char *)dynalloc_alloc(over_max);
    if (kmem_ptr7 == (void*)0) {
        uart_puts("Allocation failed as expected for size > MAX_ALLOC_SIZE\n");
    } else {
        uart_puts("Unexpected allocation success for size > MAX_ALLOC_SIZE\n");
        dynalloc_free(kmem_ptr7);
    }

    uart_puts("mmtest done.\n");
}

static void cmd_hello(void) {
    uart_puts("Hello, World.\n");
}

static void cmd_info(void) {
    bootinfo_print();
}

static void cmd_load(void) {
    bootloader_load_kernel();
}

static void cmd_unknown(const char* cmd) {
    uart_puts("Unknown command : ");
    uart_puts(cmd);
    uart_puts("\nUse help to get commands.\n");
}

static void exec_cmd(const char* cmd) {
    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "hello") == 0) {
        cmd_hello();
    } else if (strcmp(cmd, "info") == 0) {
        cmd_info();
    } else if (strcmp(cmd, "ls") == 0) {
        initrd_list(g_initrd_base);
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        initrd_cat(g_initrd_base, cmd + 4);
    } else if (strcmp(cmd, "load") == 0) {
        cmd_load();
    } else if (strcmp(cmd, "mmtest") == 0) {
        cmd_mmtest();
    } else {
        cmd_unknown(cmd);
    }
}

void shell(void) {
    uart_puts("opi-rv2> ");
    while (1) {
        char c = uart_getc();
        if (c == '\n') {
            cmd_buffer[cmd_ptr] = '\0';
            uart_putc('\n');
            if (cmd_ptr > 0) {
                exec_cmd(cmd_buffer);
            }
            cmd_ptr = 0;
            break;
        } else if (c == '\b' || c == 127) {
            if (cmd_ptr > 0) {
                cmd_ptr--;
                uart_putc('\b');
                uart_putc(' ');
                uart_putc('\b');
            }
        } else {
            if (cmd_ptr < MAX_CMD_LEN - 1) {
                cmd_buffer[cmd_ptr++] = c;
            }
            uart_putc(c);
        }
    }
}
