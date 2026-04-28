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
    puts_("OSC test user program\n");
    char buf[128];
    while (1) {
        puts_("$ ");
        int n = readline(buf, sizeof(buf));
        if (n == 0) continue;
        if      (strcmp(buf, "help") == 0) cmd_help();
        else if (strcmp(buf, "pid")  == 0) cmd_pid();
        else if (strcmp(buf, "exit") == 0) exit(0);
        else if (strcmp(buf, "fork_test") == 0) fork_test();
        else                                puts_("Unknown command\n");
    }
}
