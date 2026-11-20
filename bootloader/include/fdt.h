#ifndef FDT_H
#define FDT_H

#include <stdint.h> // define uintptr_t:把記憶體位址（指標）安全地變成一個數字（整數），讓你能夠對位址進行加減、位元運算，且保證不弄丟任何位元。

#define FDT_MAGIC      0xd00dfeed
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

uint32_t bswap32(uint32_t x);
uint64_t bswap64(uint64_t x);
int fdt_path_offset(const void* fdt, const char* path);
const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp);
// 回傳整份 DTB 的 totalsize（已做 endian 轉換）。magic 不對或 fdt 為 NULL 時回傳 0。
unsigned long fdt_total_size(const void* fdt);

// 回傳 root node 的 offset（即 structure block 第一個 FDT_BEGIN_NODE token 的位置）。
// magic 不對或 fdt 為 NULL 時回傳 -1。
int fdt_root_offset(const void* fdt);

// 從 parent node 讀 #address-cells / #size-cells，缺省套用 DT spec 預設值 (ac=2, sc=1)。
// parent_offset < 0 時也套用預設。
void fdt_get_cells(const void* fdt, int parent_offset, int* address_cells, int* size_cells);

// 從 big-endian cells 指標讀 1 或 2 個 cell 組成的整數。cells 必須為 1 或 2。
unsigned long fdt_read_cells(const uint32_t* p, int cells);

// 由 parent_offset 的 #address-cells/#size-cells 決定編碼，從 node 的 reg 屬性讀第 index 筆。
// 成功回 0，失敗回 -1（節點無 reg、長度不足、或 cells 非法）。
int fdt_get_reg(const void* fdt, int parent_offset, int node_offset, int index,
                unsigned long* base, unsigned long* size);

int fdt_first_subnode(const void* fdt, int parent_offset);
int fdt_next_subnode(const void* fdt, int node_offset);

unsigned long fdt_get_cpu_freq(const void* fdt);

#endif
