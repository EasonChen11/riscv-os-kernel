#ifndef PLIC_H
#define PLIC_H

// 初始化：從 DTB 解出 PLIC base、依 hartid 算 S-mode context、
// 清 threshold（全收）。必須在 plic_enable_irq 之前呼叫。
void plic_init(const void *fdt, unsigned long hartid);

// 設定某個 IRQ 的優先權並在當前 context 的 enable bitmap 中開啟它。
// priority=0 代表關閉，>0 為啟用。
void plic_enable_irq(unsigned int irq, unsigned int priority);

// 讀 claim register：硬體回傳當前優先權最高且已 pending 的 IRQ 號，
// 並清除該 IRQ 的 pending bit。無 pending 時回傳 0。
unsigned int plic_claim(void);

// 寫 complete register：告訴 PLIC 這個 IRQ 已處理完畢，
// 讓後續同號中斷可再次被送達。**漏寫會造成該線路永久 stuck**。
void plic_complete(unsigned int irq);

#endif
