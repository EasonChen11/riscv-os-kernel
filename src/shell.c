#include "shell.h"
#include "uart.h"
#include "cpio.h"
#include "string.h"
#include "bootinfo.h"
#include "buddy.h"
#include "dynalloc.h"
#include "trap.h"
#include "timer.h"
#include "video.h"
#include "proc.h"
#include "exec.h"

static char cmd_buffer[MAX_CMD_LEN];
static int cmd_ptr = 0;

void shell_init(void) {
}

// ─── shell command handlers ────────────────────────────────────
static void cmd_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help       - show all commands.\n");
    uart_puts("  hello      - print Hello, World.\n");
    uart_puts("  info       - print system info.\n");
    uart_puts("  ls         - list initrd files.\n");
    uart_puts("  cat        - print file content. usage: cat <filename>\n");
    uart_puts("  mmtest     - run buddy allocator test.\n");
    uart_puts("  exec       - execute user program. usage: exec <filename>\n");
    uart_puts("  setTimeout - schedule a message. usage: setTimeout <sec> <message>\n");
    uart_puts("  heartbeat  - toggle heartbeat output (boot time print) on/off.\n");
    uart_puts("  vidtest    - draw a 256x256 RGB gradient via video_bmp_display.\n");
}

// vidtest：在 framebuffer 中央畫一張 256x256 漸層方塊
// QEMU 預設 -display none 看不到視窗，要拿掉或加 -display gtk 才看得到
// OrangePi RV2 板上插 HDMI 直接看得到
static void cmd_vidtest(void) {
    static unsigned int tile[256 * 256];
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            unsigned int r = x;          // 紅 X 軸漸層
            unsigned int g = y;          // 綠 Y 軸漸層
            unsigned int b = (x + y) / 2;
            tile[y * 256 + x] = (r << 16) | (g << 8) | b;
        }
    }
    video_bmp_display(tile, 256, 256);
    uart_puts("vidtest: 256x256 gradient drawn at framebuffer center\n");
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

// 解析 "setTimeout SEC MESSAGE"，MESSAGE 允許含空白。
static void cmd_setTimeout(const char* args) {
    while (*args == ' ') args++;
    if (*args < '0' || *args > '9') {
        uart_puts("usage: setTimeout <sec> <message>\n");
        return;
    }
    int sec = 0;
    while (*args >= '0' && *args <= '9') {
        sec = sec * 10 + (*args - '0');
        args++;
    }
    if (*args != ' ') {
        uart_puts("usage: setTimeout <sec> <message>\n");
        return;
    }
    while (*args == ' ') args++;
    if (*args == '\0') {
        uart_puts("setTimeout: empty message\n");
        return;
    }
    setTimeout(sec, args);
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
    } else if (strcmp(cmd, "mmtest") == 0) {
        cmd_mmtest();
    } else if (strncmp(cmd, "exec ", 5) == 0) {
        const char *path = cmd + 5;
        if (*path == 0) {
            uart_puts("usage: exec <filename>\n");
        } else {
            long pid = spawn_user(path); // create child process running user program; return pid or -1 on failure
            do_waitpid(pid);
        }
    } else if (strncmp(cmd, "setTimeout ", 11) == 0) {
        cmd_setTimeout(cmd + 11);
    } else if (strcmp(cmd, "heartbeat") == 0) {
        int q = toggle_heartbeat();
        uart_puts(q ? "heartbeat: quiet\n" : "heartbeat: verbose\n");
    } else if (strcmp(cmd, "vidtest") == 0) {
        cmd_vidtest();
    } else {
        cmd_unknown(cmd);
    }
}

void shell_thread(void) {
    // kernel thread 從 idle schedule 切過來時 SIE 繼承為 0，自己 enable 才會被 preempt
    asm volatile("csrsi sstatus, 2");
    shell_init();
    while (1) {
        shell();
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
