#include "uart.h"
#include "fdt.h"
#include "string.h"
#include "plic.h"

unsigned long uart_base  = 0x0L;   // DTB 解析前為 0；解析後填入
unsigned long uart_shift = 0x0L;   // 0 for 16550A(QEMU), 2 for DW UART (4-byte stride)

static unsigned int uart_irq_num = 0;   // UART 對 PLIC 的 IRQ 號（由 uart_init 從 DTB 讀）

void uart_init(const void* fdt) {
    // QEMU: /soc/uart, OrangePi RV2: /soc/serial
    int offset = fdt_path_offset(fdt, "/soc/serial");
    if (offset < 0) {
        offset = fdt_path_offset(fdt, "/soc/uart");
    }

    int soc = fdt_path_offset(fdt, "/soc");
    unsigned long addr = 0, size = 0;
    if (fdt_get_reg(fdt, soc, offset, 0, &addr, &size) < 0) {
        uart_puts("uart_init: reg parse failed\n");
        return;
    }
    if (addr != 0)
        uart_base = addr;

    // reg-shift：16550A 沒這屬性預設 0；DW/SiFive 為 2
    int shift_size = 0;
    const void* prop = fdt_getprop(fdt, offset, "reg-shift", &shift_size);
    if (prop) {
        uart_shift = fdt_read_cells((const uint32_t*)prop, 1);
    } else {
        uart_shift = 0x0L;
    }

    // interrupts：UART 對 PLIC 的 IRQ 號
    int irq_size = 0;
    const void* irq_prop = fdt_getprop(fdt, offset, "interrupts", &irq_size);
    if (irq_prop) {
        uart_irq_num = fdt_read_cells((const uint32_t*)irq_prop, 1);
    }
}

// 在 uart_irq_init 之後呼叫，否則 interrupt 路徑的 TX ring 沒人 drain
void uart_init_print_info(void) {
    uart_puts("uart_init: base=0x");
    uart_hex(uart_base);
    uart_puts(" shift=");
    uart_int(uart_shift);
    uart_puts(" irq=");
    uart_int(uart_irq_num);
    uart_putc('\n');
}

// uart_putc 在 #if 分支裡個別實作（polling / interrupt 行為不同）
void uart_putc(char c);

void uart_puts(const char* s) {
    while (*s)
        uart_putc(*s++);
}

void uart_hex(unsigned long h) {
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}

void uart_int(unsigned long n) {
    if (n == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--)
        uart_putc(buf[j]);
}

#if MODE == 0
// ======== Polling TX / RX ========

void uart_putc(char c) {
    if (c == '\n')
        uart_putc('\r');
    while ((*UART_LSR & LSR_TDRQ) == 0)
        ;
    *UART_THR = c;
}

char uart_getc(void) {
    while ((*UART_LSR & LSR_DR) == 0)
        ;
    char c = (char)*UART_RBR;
    return c == '\r' ? '\n' : c;
}

bool uart_try_getc(char *out) {
    if ((*UART_LSR & LSR_DR) == 0) return false;
    char c = (char)*UART_RBR;
    *out = (c == '\r') ? '\n' : c;
    return true;
}

unsigned char uart_getb(void) {
    while ((*UART_LSR & LSR_DR) == 0)
        ;
    return *UART_RBR;
}

// polling 模式不使用 PLIC 中斷，提供 no-op 讓 main/trap 可無條件呼叫
void uart_irq_init(void) {}
void uart_handle_irq(unsigned int irq) { (void)irq; }

#else
// ======== Interrupt TX / RX ========

// SPSC ring：RX 的 producer 是 ISR、consumer 是 shell；TX 反過來
// empty: head == tail；full: (head + 1) % SZ == tail（留一格區分）
#define RX_BUF_SZ 128
static volatile unsigned char rx_buf[RX_BUF_SZ];
static volatile unsigned int  rx_head;   // ISR 寫入點
static volatile unsigned int  rx_tail;   // shell 讀取點

#define TX_BUF_SZ 4096
static volatile unsigned char tx_buf[TX_BUF_SZ];
static volatile unsigned int  tx_head;   // uart_putc 寫入點
static volatile unsigned int  tx_tail;   // ISR 讀取點

static inline void rx_put(unsigned char c) {
    unsigned int next = (rx_head + 1) % RX_BUF_SZ;
    if (next == rx_tail) return;   // 滿了丟最新，避免蓋掉 shell 還沒讀的歷史字元
    rx_buf[rx_head] = c;
    rx_head = next;
}

static inline bool rx_get(unsigned char *c) {
    if (rx_head == rx_tail) return false;
    *c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SZ;
    return true;
}

static inline bool tx_is_full(void)  { return ((tx_head + 1) % TX_BUF_SZ) == tx_tail; }
static inline bool tx_is_empty(void) { return tx_head == tx_tail; }

static inline void tx_put(unsigned char c) {
    tx_buf[tx_head] = c;
    tx_head = (tx_head + 1) % TX_BUF_SZ;
}

static inline bool tx_get(unsigned char *c) {
    if (tx_head == tx_tail) return false;
    *c = tx_buf[tx_tail];
    tx_tail = (tx_tail + 1) % TX_BUF_SZ;
    return true;
}

// 關 / 還原 sstatus.SIE（保護 IER 與 tx_head 不被 ISR 搶）
static inline unsigned long irq_save(void) {
    unsigned long s;
    asm volatile("csrrc %0, sstatus, %1" : "=r"(s) : "i"(1 << 1)); // clear sstatus.SIE，關閉全域中斷
    return s;
}
static inline void irq_restore(unsigned long s) {
    asm volatile("csrs sstatus, %0" :: "r"(s & (1 << 1)));
}

void uart_irq_init(void) {
    *UART_LCR &= ~(1 << 7);                    // DLAB=0，確保 IER 訪問 IER 而非 DLH
    *UART_FCR = (1 << 0) | (1 << 1) | (1 << 2);// FIFO enable + clr RX + clr TX
    *UART_MCR |= (1 << 3);                     // OUT2：IRQ 接 PLIC
    plic_enable_irq(uart_irq_num, 1);          // PLIC 端 enable
    *UART_IER |= (1 << 0);                     // ERBFI；|= 保留 u-boot 留下的 bit（OrangePi RV2 DW UART 的 bit 6 不可清）
}

// 由 trap 的 INT_SUP_EXT 分支呼叫。一次 IRQ 可能同時帶 RX 和 TX 事件，都要處理
void uart_handle_irq(unsigned int irq) {
    if (irq != uart_irq_num) return;

    // RX：抽乾 FIFO 到 ring
    while (*UART_LSR & LSR_DR) {
        unsigned char c = *UART_RBR;       // 讀 RBR 會清 LSR.DR；ISR 是唯一讀者
        rx_put(c);
    }

    // TX：THR empty 時把 ring 搬到 THR；ring 空就關 ETBEI 避免 livelock
    while (!tx_is_empty() && (*UART_LSR & LSR_TDRQ)) {
        unsigned char c;
        tx_get(&c);
        *UART_THR = c;
    }
    if (tx_is_empty()) {
        *UART_IER &= ~(1 << 1);                // 清 ETBEI
    }
}

void uart_putc(char c) {
    if (c == '\n')
        uart_putc('\r');

    // 進 spin 不需要再開 ETBEI：
    //   - 只有前一次 uart_putc 成功 enqueue 過後，ring 才會非空；而那次結尾有 |= ETBEI
    //   - ISR 僅在 tx_is_empty() 時才清 ETBEI
    // 故 ring 非空 ⇒ ETBEI=1 的 invariant 由 producer 與 consumer 合作維持，ISR 一定會來 drain
    while (tx_is_full())
        ;

    unsigned long saved = irq_save();      // 保護 tx_head 與 IER 不被 ISR 搶
    tx_put((unsigned char)c);
    *UART_IER |= (1 << 1);                 // producer 職責：加料後確保 ETBEI=1，讓 ISR 來搬
    irq_restore(saved);
}

bool uart_try_getc(char *out) {
    unsigned char c;
    if (!rx_get(&c)) return false;
    *out = (c == '\r') ? '\n' : (char)c;
    return true;
}

char uart_getc(void) {
    char c;
    while (!uart_try_getc(&c))
        ;                                  // spin；之後可換成 wfi
    return c;
}

// bootloader 專用：讀 raw byte（不做 \r→\n 轉換）。
// interrupt 模式下 ISR 是 RBR 唯一讀者，這裡只能從 rx ring 取；
// rx ring 存的是 ISR 直接從 RBR 讀來的原始 byte，沒經過 \r→\n 翻譯，符合 bootloader 語意
unsigned char uart_getb(void) {
    unsigned char c;
    while (!rx_get(&c))
        ;
    return c;
}

#endif
