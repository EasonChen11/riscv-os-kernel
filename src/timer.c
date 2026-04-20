#include "timer.h"
#include "sbi.h"
#include "uart.h"
#include "fdt.h"
#include "bootinfo.h"

static unsigned long boot_time = 0;
static unsigned long CPU_FREQ = 10000000; // 10 MHz

static void log_timer(unsigned long now_ms) {
    uart_puts("boot time: ");
    uart_int((now_ms - boot_time) / CPU_FREQ);
    uart_puts("\n");
}

void timer_init() {
    CPU_FREQ = fdt_get_cpu_freq(g_fdt_base); // 從 FDT /cpus timebase-frequency 讀，不可硬編碼
    unsigned long now_ms = 0;
    asm volatile("rdtime %0" : "=r"(now_ms)); // rdtime 讀 mtime shadow（tick，非毫秒）
    boot_time = now_ms;
    sbi_set_timer(now_ms + 2*CPU_FREQ); // 傳絕對 mtime 值，2 秒後第一次觸發
    asm volatile("csrrs zero, sie, %0" :: "r"(1 << 5));  // sie.STIE = bit 5，開 timer 中斷線
    asm volatile("csrsi sstatus, 0x2"); // sstatus.SIE = bit 1，全域中斷開關，兩個都要開
    log_timer(now_ms);
}

void handle_timer_interrupt() {
    unsigned long now_ms = 0;
    asm volatile("rdtime %0" : "=r"(now_ms));
    log_timer(now_ms);
    sbi_set_timer(now_ms + 2*CPU_FREQ); // set next timer interrupt for 2 seconds later
}