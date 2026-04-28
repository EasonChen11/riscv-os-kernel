#ifndef TIMER_H
#define TIMER_H

#include "list.h"

#define TIMER_MAX_EVENTS   32
#define TIMER_MSG_LEN      96

typedef void (*timer_callback_t)(void*);

struct timer_event {
    struct list_head  link;          // sorted queue 節點
    unsigned long     expire_tick;   // 到期的絕對 mtime
    timer_callback_t  callback;      // 到期時要延後到 task 的動作
    void             *arg;           // callback 參數；語意由 caller 決定（setTimeout 指向獨立 ctx）
    unsigned char     in_use;        // pool slot 狀態（0=free, 1=busy）
};

void timer_init(void);
void handle_timer_interrupt(void);

// 註冊 one-shot timer：sec 秒後呼叫 cb(arg)。
// 靜態 pool 滿或 sec <= 0 時印錯誤並返回。
// 可在 ISR 或 thread 上下文呼叫（內部自己 irq_save）。
void add_timer(timer_callback_t cb, void* arg, int sec);

// sub-second 版本：直接接 raw mtime ticks，preempt tick / usleep 用。
// 內部行為跟 add_timer 一致，差別只在不做 sec * CPU_FREQ 轉換。
void add_timer_ticks(timer_callback_t cb, void* arg, unsigned long ticks);

// 取 CPU mtime 的 frequency（Hz），讓 caller 自己算 ticks。
// schedule 用它算 1/32 秒對應幾個 tick（CPU_FREQ / 32）。
unsigned long timer_get_freq(void);

// shell setTimeout 命令：sec 秒後輸出 "[setTimeout cmd=Xs fired=Ys] MESSAGE"。
// sec 合法範圍 1..10000；msg 會被複製到 event 內嵌 buffer（truncate 到 TIMER_MSG_LEN-1）。
void setTimeout(int sec, const char* msg);

// 切換 heartbeat 輸出開關。回傳切換後狀態：1=quiet（不印）、0=verbose（每 2 秒印）。
// 不影響 timer 節拍，cb 仍依時間排程。
int toggle_heartbeat(void);

#endif /* TIMER_H */
