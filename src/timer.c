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
    CPU_FREQ = fdt_get_cpu_freq(g_fdt_base);
    unsigned long now_ms = 0;
    asm volatile("rdtime %0" : "=r"(now_ms));
    boot_time = now_ms;
    sbi_set_timer(now_ms + 2*CPU_FREQ); // 2 seconds later
    asm volatile("csrrs zero, sie, %0" :: "r"(1 << 5));  // enable supervisor timer interrupt (SIE.STIE = 1)
    asm volatile("csrsi sstatus, 0x2"); // enable interrupts (SSTATUS.SIE = 1)
    log_timer(now_ms);
}

void handle_timer_interrupt() {
    unsigned long now_ms = 0;
    asm volatile("rdtime %0" : "=r"(now_ms));
    log_timer(now_ms);
    sbi_set_timer(now_ms + 2*CPU_FREQ); // set next timer interrupt for 2 seconds later
}