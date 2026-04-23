#ifndef IRQ_H
#define IRQ_H

// ─── sstatus.SIE (bit 1) 操作集中於此 ───────────────────────────────────
// 兩組 API，語意不同，不可混用：
//
//   irq_save / irq_restore  → 配對使用，保護臨界區（Linux local_irq_save 慣例）
//   sie_enable / sie_disable → 無條件強制切換（給 drain_tasks 跑 cb 前後用）
//
// 命名沿用 Linux 慣例（local_irq_save 也是 save+disable 一氣呵成）。
// 名稱看不出隱含 disable，但拆開做不到原子，這是慣例妥協。

// 進臨界區：「儲存舊 sstatus 並強制 disable SIE」一氣呵成（必須原子）。
//   csrrc = CSR Read and Clear，硬體保證讀+清不被 IRQ 切開。
//   回傳值要保留並交給 irq_restore；不要當布林或自行解析。
static inline unsigned long irq_save(void) {
    unsigned long s;
    asm volatile("csrrc %0, sstatus, %1" : "=r"(s) : "i"(1 << 1));
    return s;
}

// 出臨界區：「依 saved 值決定要不要重新 enable SIE」。
//   注意：本函式只能 set 方向（csrs），無法 clear SIE！
//   - saved.SIE=1 → 寫入 0x2 → 把 SIE 設為 1（還給 caller）
//   - saved.SIE=0 → 寫入 0x0 → no-op（SIE 維持當前值）
//   因此必須與 irq_save 配對使用；不可拿 irq_restore(0) 來「關 SIE」，要用 sie_disable。
static inline void irq_restore(unsigned long s) {
    asm volatile("csrs sstatus, %0" :: "r"(s & (1UL << 1)));
}

// 強制開 SIE（不看舊值、不配對）。drain_tasks 在跑 callback 前呼叫，允許 nested IRQ。
static inline void sie_enable(void)  {
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 1));
}

// 強制關 SIE（不看舊值、不配對）。drain_tasks callback 結束後呼叫，保護後續 RMW。
static inline void sie_disable(void) {
    asm volatile("csrc sstatus, %0" :: "r"(1UL << 1));
}

#endif /* IRQ_H */
