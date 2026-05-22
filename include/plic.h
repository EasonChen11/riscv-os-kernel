#ifndef PLIC_H
#define PLIC_H

// 從 DTB 解出的原始 PLIC PA + size（mmio_finalize 走 PTE level 用）
extern unsigned long plic_pa;
extern unsigned long plic_size;

// FDT 解析：取出 plic_pa/plic_size/plic_base、依 hartid 算 S-mode context。
// 純記憶體操作，不碰 MMIO；必須早於 mmio_finalize 呼叫。
void plic_init(const void *fdt, unsigned long hartid);

// MMIO 啟用：寫 PLIC threshold + 開 sie.SEIE。必須在 mmio_finalize 之後呼叫。
void plic_enable(void);

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
