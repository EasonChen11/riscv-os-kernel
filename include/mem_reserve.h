#ifndef MEM_RESERVE_H
#define MEM_RESERVE_H

void memory_reserve(unsigned long start, unsigned long size); // 將 [start, start+size) 區間記錄為 reserved（會 page-align）
int  memory_is_reserved(unsigned long page_idx); // 查 page_idx 所在頁是否落在任一被 reserve 的區間內
void memory_reserve_fdt_reserved_mem(const void* fdt); // 讀 fdt /reserved-memory 節點並逐一呼叫 memory_reserve

// 在 [search_lo, search_hi) 內、避開所有已 reserve 的區間，找出第一塊大小 ≥ size、對齊到 align 的空洞
// 成功回傳區塊起始位址；失敗回傳 0
unsigned long memory_find_free(unsigned long search_lo, unsigned long search_hi,
                               unsigned long size, unsigned long align);

#endif // MEMORY_RESERVE_H
