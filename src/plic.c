#include "plic.h"
#include "fdt.h"
#include "uart.h"

// PLIC memory map
//   BASE + 0x000000 + 4*IRQ             : priority[IRQ]
//   BASE + 0x001000 + 4*(IRQ/32)        : pending bitmap
//   BASE + 0x002000 + 0x80*CTX + 4*(IRQ/32) : enable bitmap per context
//   BASE + 0x200000 + 0x1000*CTX        : threshold
//   BASE + 0x200004 + 0x1000*CTX        : claim (R) / complete (W)
// Context 編號：S-mode = 2*hartid + 1（見 DTS interrupts-extended 陣列順序）

static unsigned long plic_base = 0;
static int plic_ctx = 1;  // 預設 hart0 S-mode

#define REG32(addr) (*(volatile unsigned int *)(addr)) // volatile 告訴編譯器這個記憶體位置可能會被硬體或其他程式改變，不要對它做優化或假設，確保每次讀寫都直接對記憶體操作

#define PLIC_PRIORITY(irq)     (plic_base + 4UL * (irq))
// 起始在 0x002000，每個 context 的 enable bitmap 區塊大小 0x80，irq/32 是因為每個 enable register 控制 32 個 IRQ 
#define PLIC_ENABLE(ctx, irq)  (plic_base + 0x002000UL + 0x80UL * (ctx) + 4UL * ((irq) / 32)) // -> => PAD_PIC_PLIC_INT_1_CTL_REG(63~32)
#define PLIC_THRESHOLD(ctx)    (plic_base + 0x200000UL + 0x1000UL * (ctx)) // context S-Mode 差 0x1000
#define PLIC_CLAIM(ctx)        (plic_base + 0x200004UL + 0x1000UL * (ctx)) // context S-Mode 差 0x1000

void plic_init(const void *fdt, unsigned long hartid) {
    // 兩塊板子節點名稱不同：QEMU 用 plic@、RV2 用 interrupt-controller@
    int node = fdt_path_offset(fdt, "/soc/plic");
    if (node < 0) node = fdt_path_offset(fdt, "/soc/interrupt-controller");
    if (node < 0) {
        uart_puts("[plic] node not found\n");
        return;
    }
    int soc = fdt_path_offset(fdt, "/soc");
    unsigned long size = 0;
    if (fdt_get_reg(fdt, soc, node, 0, &plic_base, &size) < 0) {
        uart_puts("[plic] reg parse failed\n");
        return;
    }

    plic_ctx = 2 * (int)hartid + 1;  // hart_id S-mode context

    REG32(PLIC_THRESHOLD(plic_ctx)) = 0;  // threshold=0：所有 priority>0 的都接受
    asm volatile("csrrs zero, sie, %0" :: "r"(1 << 9)); // sie.SEIE
    
    // uart_puts("[plic] base=");
    // uart_hex(plic_base);
    // uart_puts(" ctx=");
    // uart_int(plic_ctx);
    // uart_puts("\n");
}

void plic_enable_irq(unsigned int irq, unsigned int priority) {
    REG32(PLIC_PRIORITY(irq)) = priority;
    REG32(PLIC_ENABLE(plic_ctx, irq)) |= (1U << (irq % 32));
}

// 讀 claim register：硬體回傳當前優先權最高且已 pending 的 IRQ 號，並清除該 IRQ 的 pending bit。無 pending 時回傳 0。
unsigned int plic_claim(void) {
    return REG32(PLIC_CLAIM(plic_ctx));
}

// 告訴 PLIC 這個 IRQ 已處理完畢，讓後續同號中斷可再次被送達。**漏寫會造成該線路永久 stuck**。
void plic_complete(unsigned int irq) {
    REG32(PLIC_CLAIM(plic_ctx)) = irq;
}
