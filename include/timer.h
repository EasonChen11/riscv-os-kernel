#ifndef TIMER_H
#define TIMER_H

#include "list.h"

#define TIMER_MAX_EVENTS   32
#define TIMER_MSG_LEN      96

typedef void (*timer_callback_t)(void*);

struct timer_event {
    struct list_head  link;          // sorted queue 節點
    unsigned long     expire_tick;   // 到期的絕對 mtime
    unsigned long     cmd_tick;      // add_timer 呼叫時的 mtime（setTimeout 輸出用）
    timer_callback_t  callback;      // 到期動作
    void             *arg;           // callback 參數；setTimeout 會指回 event 自己
    char              msg[TIMER_MSG_LEN]; // setTimeout 訊息嵌在 event 裡，免另外 heap
    unsigned char     in_use;        // pool slot 狀態（0=free, 1=busy）
    unsigned char     transferred;   // 1=cb 已把 event ownership 轉交下游 task，handle_timer_interrupt 不再 pool_free
};

void timer_init(void);
void handle_timer_interrupt(void);

// 註冊 one-shot timer：sec 秒後呼叫 cb(arg)。
// 靜態 pool 滿或 sec <= 0 時印錯誤並返回。
// 可在 ISR 或 thread 上下文呼叫（內部自己 irq_save）。
void add_timer(timer_callback_t cb, void* arg, int sec);

// shell setTimeout 命令：sec 秒後輸出 "[setTimeout cmd=Xs fired=Ys] MESSAGE"。
// sec 合法範圍 1..10000；msg 會被複製到 event 內嵌 buffer（truncate 到 TIMER_MSG_LEN-1）。
void setTimeout(int sec, const char* msg);

#endif /* TIMER_H */
