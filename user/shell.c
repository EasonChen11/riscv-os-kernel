/* osctest user shell — Phase 1 minimum: help / pid / exit
 *
 * 規劃中（未來 phase）：
 *   exec [prog]    fork + exec 跑另一個 user binary
 *   fork_test      lab5.rst BE2 規定的 fork 測試
 *   fork           BE3 fork 出 child 跑 video player
 *   stop [pid]     SYS_STOP
 *   signal         Adv1 註冊 SIGTERM handler
 *   kill [pid]     Adv1 SYS_KILL
 *
 * 本 commit 先做 build pipeline + 最小可互動 shell；其他 command 之後分階段補。
 */

#include "lib.h"

static void cmd_help(void) {
    puts_("help    Print this help message\n");
    puts_("pid     Print the current process ID\n");
    puts_("exit    Quit this program\n");
}

static void cmd_pid(void) {
    puts_("pid = ");
    print_int(getpid());
    putc_('\n');
}

void user_main(void) {
    puts_("OSC test user program\n");
    char buf[128];
    while (1) {
        puts_("$ ");
        int n = readline(buf, sizeof(buf));
        if (n == 0) continue;
        if      (strcmp(buf, "help") == 0) cmd_help();
        else if (strcmp(buf, "pid")  == 0) cmd_pid();
        else if (strcmp(buf, "exit") == 0) exit(0);
        else                                puts_("Unknown command\n");
    }
}
