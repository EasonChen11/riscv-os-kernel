#include "shell.h"
#include "uart.h"
#include "cpio.h"
#include "string.h"
#include "bootinfo.h"
#include "trap.h"
#include "timer.h"
#include "proc.h"
#include "exec.h"
#include "vfs.h"

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
    uart_puts("  exec       - execute user program. usage: exec <filename>\n");
    uart_puts("  setTimeout - schedule a message. usage: setTimeout <sec> <message>\n");
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

// 走 VFS 列出 /ramfs（唯讀鏡射 initramfs.cpio）下所有檔案的 size 與名稱
static void cmd_ls(void) {
    char name[VFS_MAX_COMPONENT];
    uint64_t size;
    int count = 0;
    while (vfs_readdir("/ramfs", count, name, &size) == E_OK) count++;
    uart_puts("Total ");
    uart_int(count);
    uart_puts(" files:\n");
    for (int i = 0; vfs_readdir("/ramfs", i, name, &size) == E_OK; i++) {
        uart_int((int)size);
        uart_putc('\t');
        uart_puts(name);
        uart_putc('\n');
    }
    uart_putc('\n');
}

// 走 VFS 開 /ramfs/<name> 並把內容逐塊讀出印到終端
static void cmd_cat(const char* name) {
    char path[VFS_MAX_PATH];
    int pi = 0;
    for (const char* s = "/ramfs/"; *s && pi < VFS_MAX_PATH - 1; s++) path[pi++] = *s;
    for (const char* s = name;      *s && pi < VFS_MAX_PATH - 1; s++) path[pi++] = *s;
    path[pi] = '\0';

    struct file* f;
    if (vfs_open(path, 0, &f) != E_OK) {
        uart_puts("File not found\n");
        return;
    }
    char buf[64];
    int k;
    while ((k = vfs_read(f, buf, sizeof(buf))) > 0)
        for (int i = 0; i < k; i++) uart_putc(buf[i]);
    uart_putc('\n');
    vfs_close(f);
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
        cmd_ls();                              // 走 VFS 列 /ramfs
        // 舊路徑（直讀 cpio archive）：initrd_list(g_initrd_base);
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        cmd_cat(cmd + 4);                      // 走 VFS 讀 /ramfs/<file>
        // 舊路徑（直讀 cpio archive）：initrd_cat(g_initrd_base, cmd + 4);
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
