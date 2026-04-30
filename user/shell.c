/* osctest user shell — 對齊 lab5.rst 規格的最小 shell
 *
 * 已支援：help / pid / exit / fork_test / exec / fork / stop / signal / kill
 */

#include "lib.h"

#define SIGTERM 15

static void cmd_help(void) {
    puts("help            Print this help message");
    puts("pid             Print the current process ID");
    puts("exit            Quit this program");
    puts("fork_test       Run nested-fork demo (lab5.rst BE2)");
    puts("exec [prog]     Run [prog] as a child process");
    puts("fork            Fork a child (Adv1 signal demo target)");
    puts("stop [pid]      Forcibly terminate process [pid]");
    puts("signal          Register SIGTERM handler (subsequent forks inherit)");
    puts("kill [pid]      Send SIGTERM to [pid]");
}

static void cmd_pid(void) {
    printf("pid = %d\n", getpid());
}

/* exec [prog]：fork 出 child 去 exec，shell 自己 waitpid 等 child 結束。
 * 直接呼叫 exec 會把 shell 換成 prog 回不來，所以要包 fork+exec+waitpid。 */
static void cmd_exec(const char* arg) {
    if (*arg == 0) { puts("usage: exec <prog>"); return; }
    long pid = fork();
    if (pid == 0) {
        exec(arg);
        puts("exec failed");
        exit(1);
    } else if (pid > 0) {
        waitpid(pid);
    } else {
        puts("fork failed");
    }
}

/* fork：fork 出 child 進 idle，shell 不 wait 直接回 prompt。
 * 給 lab5.rst Adv1 signal demo 用：signal → fork → kill <child_pid>。 */
static void cmd_fork(void) {
    long pid = fork();
    if (pid == 0) {
        printf("child: pid = %d, waiting for signal...\n", getpid());
        while (1) usleep(100000);
        // unreachable
    } else if (pid > 0) {
        printf("parent: child pid = %d\n", pid);
    } else {
        puts("fork failed");
    }
}

static void cmd_stop(const char* arg) {
    if (*arg == 0) { puts("usage: stop <pid>"); return; }
    long pid = atoi(arg);
    if (stop(pid) < 0) {
        printf("stop failed: pid %d\n", pid);
    }
}

/* lab5 Adv1 demo handler：在 user mode 跑、不可呼 stdlib syscall 之外的東西。
 * uart_write 是合法 syscall，但 printf 走的也是它，可以放心用。
 * 跑完 ret 進 trampoline → SYS_SIGRETURN 回到原本 usleep 中斷點。 */
static void sig_handler(void) {
    puts("signal handler!");
}

/* signal：對當前 process 註冊 SIGTERM handler。
 * 規格規定只有「之後 fork 出來的 child」會繼承（do_fork 用 *c = *p 淺拷貝
 * signal_handlers），現在 fork 出去的 child 都帶這份 handler，被 kill 不會死、
 * 而是跑 sig_handler。 */
static void cmd_signal(void) {
    if (signal(SIGTERM, sig_handler) < 0) {
        puts("signal failed");
    } else {
        puts("registered SIGTERM handler");
    }
}

static void cmd_kill(const char* arg) {
    if (*arg == 0) { puts("usage: kill <pid>"); return; }
    long pid = atoi(arg);
    if (kill(pid, SIGTERM) < 0) {
        printf("kill failed: pid %d\n", pid);
    }
}

void fork_test() {
    printf("Fork test (pid = %d)\n", getpid());
    int cnt = 1;
    int ret = 0;
    if ((ret = fork()) == 0) {
        long cur_sp;
        asm volatile("mv %0, sp" : "=r"(cur_sp));
        printf("child1: pid = %d, cnt = %d, &cnt = %p, sp = %p\n", getpid(), cnt, &cnt, cur_sp);
        cnt++;

        if ((ret = fork()) != 0) {
            asm volatile("mv %0, sp" : "=r"(cur_sp));
            printf("child1: pid = %d, cnt = %d, &cnt = %p, sp = %p\n", getpid(), cnt, &cnt, cur_sp);
            waitpid(ret);
        } else {
            while (cnt < 5) {
                asm volatile("mv %0, sp" : "=r"(cur_sp));
                printf("child2: pid = %d, cnt = %d, &cnt = %p, sp = %p\n", getpid(), cnt, &cnt, cur_sp);
                for (int i = 0; i < 1000000000; i++);
                cnt++;
            }
        }
    } else {
        printf("parent: pid = %d, child pid = %d\n", getpid(), ret);
        waitpid(ret);
    }
    exit(0);
}

void user_main(void) {
    puts("OSC test user program");
    char buf[128];
    while (1) {
        printf("$ ");
        int n = readline(buf, sizeof(buf));
        if (n == 0) continue;
        if      (strcmp(buf, "help")      == 0) cmd_help();
        else if (strcmp(buf, "pid")       == 0) cmd_pid();
        else if (strcmp(buf, "exit")      == 0) exit(0);
        else if (strcmp(buf, "fork_test") == 0) fork_test();
        else if (strcmp(buf, "fork")      == 0) cmd_fork();
        else if (strcmp(buf, "signal")    == 0) cmd_signal();
        else if (strncmp(buf, "exec ", 5) == 0) cmd_exec(buf + 5);
        else if (strncmp(buf, "stop ", 5) == 0) cmd_stop(buf + 5);
        else if (strncmp(buf, "kill ", 5) == 0) cmd_kill(buf + 5);
        else                                    puts("Unknown command");
    }
}
