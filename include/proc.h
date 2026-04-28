#ifndef PROC_H
#define PROC_H

#include "trap.h"

// 由 sys_fork wrapping；parent 拿到 child pid，child 從 ret_from_exception 進 U-mode 拿到 0
long do_fork(struct trap_frame *parent_tf);

// 等 child exit；pid=-1 表示任意 child。沒有對應 child 時 caller 標 WAITING、被 schedule 切走
long do_waitpid(long pid);

// 將自己標 ZOMBIE、喚醒等候 parent、schedule 切走（不返回）
void do_exit(int status);

// 強制終止他人（不能殺自己）；目標被搬進 zombie_list 由 idle 收屍
long do_stop(long pid);

// 用 pid 反查 thread；找不到或目標已是 ZOMBIE 都回 NULL
struct thread *find_pid(int pid);

// 把當前 thread 標 WAITING 並排 timer，usec 微秒後被 sleep_wake 喚回 run queue。
// 醒來才 return 0；caller 不需要 retry。
long do_usleep(unsigned int usec);

#endif /* PROC_H */
