#ifndef SHELL_H
#define SHELL_H

#define MAX_CMD_LEN 256

void shell(void);
void shell_init(void);

// kernel thread 入口：shell_init 一次 + 無窮迴圈呼叫 shell()。
// main 用 kthread_create(shell_thread) 接回；preemption 開了之後跟 user
// 程式可以併行，timer 切到 shell 時恢復跑互動式介面。
void shell_thread(void);

#endif // SHELL_H