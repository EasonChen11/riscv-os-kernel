/***
fdt have four main block each block is only one in the DTB:
1. header: the first 40 bytes of the DTB, contains the magic number, the total size of the DTB, the offset and size of the structure block and the strings block, and some other information
2. memory reservation block: a sequence of 16-byte entries that specify the reserved memory regions for the platform, each entry has a 64-bit address and a 64-bit size, and the block ends with an entry of all zeros
3. structure block: a sequence of tokens that describe the device tree structure, including nodes and properties. each node starts with a FDT_BEGIN_NODE token, 
    followed by the node name (null-terminated string), then the properties and child nodes, and ends with a FDT_END_NODE token. each property starts with a FDT_PROP token, followed by the property
4. strings block: a block of null-terminated strings that are referenced by the properties in the structure block. each property has a name offset that points to a string in this block, which is the name of the property
*/
#include "fdt.h"
#include "string.h"

uint32_t bswap32(uint32_t x) {
    return ((x & 0xFF000000) >> 24) |
           ((x & 0x00FF0000) >> 8)  |
           ((x & 0x0000FF00) << 8)  |
           ((x & 0x000000FF) << 24);
}

uint64_t bswap64(uint64_t x) {
    return ((uint64_t)bswap32(x & 0xFFFFFFFF) << 32) |
           bswap32(x >> 32);
}

static inline const void* align_up(const void* ptr, unsigned long align) {
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
} //ptr + align 1 強制對齊到下一個 align 的倍數，& ~(align - 1) 是將低 align 位清零，確保對齊

int fdt_path_offset(const void* fdt, const char* path) {
    struct fdt_header* header = (struct fdt_header*)fdt;
    if(bswap32(header->magic) != FDT_MAGIC) {
        return -1;
    }
    const char* struct_base = (const char*)fdt + bswap32(header->off_dt_struct);
    const char* ptr = struct_base;
    int current_depth = 0, searching_depth = 0;
    const char* path_start = path;
    const char* path_end;
    while (1) {
        uint32_t token = bswap32(*(const uint32_t*)ptr);
        ptr += 4;
        switch (token) {
            case FDT_BEGIN_NODE: {
                const char* name = ptr;
                path_end = strchr(path_start, '/');
                if (!path_end) {
                    path_end = path_start + strlen(path_start);
                }
                unsigned long len = path_end - path_start;
                // if the current node name matches the current path component we are looking for, and the node is at the depth we are looking for, then we go into this node and look for the next path component in the children nodes
                if (current_depth == searching_depth &&
                    strncmp(name, path_start, len) == 0) {
                    char name_end = name[len];
                    if(name_end == '\0' || name_end == '@') {// allow the node name in FDT to have an optional unit address suffix after '@', but it should not be considered when comparing with the path component
                        if (*path_end == '\0') {
                            return ptr - 4 - (const char*)fdt; // roll back to the FDT_BEGIN_NODE token and return the offset of this node
                        }
                        searching_depth++;
                        path_start = path_end + 1; // move to the next component in the path, +1 to skip the '/' character
                    }
                }
                ptr += strlen(name) + 1; // skip the node name and the null terminator
                ptr = align_up(ptr, 4);
                current_depth++;
                break;
            }
            case FDT_END_NODE:
                current_depth--;
                if (current_depth < searching_depth) {
                    return -1;
                }
                break;
            case FDT_PROP: {
                int prop_len = bswap32(*(const uint32_t*)ptr);
                ptr += 8; // skip the prop length and name offset each 4 bytes
                ptr += prop_len;
                ptr = align_up(ptr, 4);
                break;
            }
            case FDT_NOP:
                break;
            case FDT_END:
                return -1;
            default:
                return -1;
        }
    }
    return -1;
}

// 回傳整份 DTB 的 totalsize（native 64-bit）
unsigned long fdt_total_size(const void* fdt) {
    if (!fdt)
        return 0;
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;
    if (bswap32(hdr->magic) != FDT_MAGIC)
        return 0;
    return bswap32(hdr->totalsize);
}

// 回傳 structure block 開頭，也就是 root node 的 FDT_BEGIN_NODE token offset
int fdt_root_offset(const void* fdt) {
    if (!fdt)
        return -1;
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;
    if (bswap32(hdr->magic) != FDT_MAGIC)
        return -1;
    return (int)bswap32(hdr->off_dt_struct);
}

// 從 parent node 讀 #address-cells / #size-cells，缺省值依 DT spec：ac=2, sc=1
void fdt_get_cells(const void* fdt, int parent_offset, int* address_cells, int* size_cells) {
    int ac = 2, sc = 1;
    if (parent_offset >= 0) {
        int len = 0;
        const void* p = fdt_getprop(fdt, parent_offset, "#address-cells", &len);
        if (p && len >= 4)
            ac = (int)bswap32(*(const uint32_t*)p);
        p = fdt_getprop(fdt, parent_offset, "#size-cells", &len);
        if (p && len >= 4)
            sc = (int)bswap32(*(const uint32_t*)p);
    }
    if (address_cells) *address_cells = ac;
    if (size_cells)    *size_cells    = sc;
}

// 從 big-endian cells 指標讀 1 或 2 個 cell。其他 cells 值視為 0。
unsigned long fdt_read_cells(const uint32_t* p, int cells) {
    if (cells == 1)
        return bswap32(p[0]);
    if (cells == 2)
        return ((unsigned long)bswap32(p[0]) << 32) | bswap32(p[1]);
    return 0;
}

// 用 parent 的 #address-cells / #size-cells 解出 node 第 index 筆 reg
int fdt_get_reg(const void* fdt, int parent_offset, int node_offset, int index,
                unsigned long* base, unsigned long* size) {
    int ac, sc;
    fdt_get_cells(fdt, parent_offset, &ac, &sc);
    if ((ac != 1 && ac != 2) || (sc != 1 && sc != 2))
        return -1;

    int len = 0;
    const void* reg = fdt_getprop(fdt, node_offset, "reg", &len);
    if (!reg)
        return -1;

    int stride_cells = ac + sc;
    int need_bytes = (index + 1) * stride_cells * 4;
    if (len < need_bytes)
        return -1;

    const uint32_t* cells = (const uint32_t*)reg + index * stride_cells;
    if (base) *base = fdt_read_cells(cells, ac);
    if (size) *size = fdt_read_cells(cells + ac, sc); // +ac to skip the address cells and get to the size cells
    return 0;
}

// 給 parent node 的 offset，回傳第一個直接子節點的 offset；無子節點回 -1
int fdt_first_subnode(const void* fdt, int parent_offset) {
    const char* ptr = (const char*)fdt + parent_offset;
    int seen_parent = 0; // 是否已 consume 掉 parent 自己的 BEGIN_NODE
    while (1) {
        uint32_t token = bswap32(*(const uint32_t*)ptr);
        ptr += 4;
        switch (token) {
            case FDT_BEGIN_NODE:
                if (!seen_parent) {
                    // 第一次遇到的 BEGIN_NODE = parent 自己，吃掉 name 繼續走
                    ptr += strlen(ptr) + 1;
                    ptr = align_up(ptr, 4);
                    seen_parent = 1;
                    break;
                }
                // 第二次遇到的 BEGIN_NODE = first child，倒退 4 byte 回到 token
                return (int)(ptr - 4 - (const char*)fdt);
            case FDT_END_NODE:
                return -1; // parent 沒有任何 child
            case FDT_PROP: {
                int prop_len = bswap32(*(const uint32_t*)ptr);
                ptr += 8 + prop_len; // 8 = 4 bytes for prop_len and 4 bytes for name_offset
                ptr = align_up(ptr, 4);
                break;
            }
            case FDT_NOP:
                break;
            case FDT_END:
            default:
                return -1;
        }
    }
}

// 給某個 node 的 offset，回傳下一個兄弟節點的 offset；無下一個回 -1
int fdt_next_subnode(const void* fdt, int node_offset) {
    const char* ptr = (const char*)fdt + node_offset;
    int depth = 0; // 0 = 還沒進入傳入 node；>0 = 在傳入 node 的內部
    while (1) {
        uint32_t token = bswap32(*(const uint32_t*)ptr);
        ptr += 4;
        switch (token) {
            case FDT_BEGIN_NODE:
                if (depth == 0) {
                    // 已經跳出傳入 node，現在這個 BEGIN_NODE 就是 sibling
                    // 但若這是「傳入 node 自己」（第一次進來），depth 還是 0 → 要區分
                    // 用 depth 從 0→1 表示「進入傳入 node」，跳過 name 後繼續
                    ptr += strlen(ptr) + 1;
                    ptr = align_up(ptr, 4);
                    depth = 1;
                    break;
                }
                // depth >= 1，表示在傳入 node 內部又遇到 child/grandchild
                ptr += strlen(ptr) + 1;
                ptr = align_up(ptr, 4);
                depth++;
                break;
            case FDT_END_NODE:
                if (depth == 0) return -1; // parent 結束，沒 sibling
                depth--;
                if (depth == 0) {
                    // 已跳出傳入 node，往後找下一個 BEGIN_NODE 當 sibling
                    while (1) {
                        uint32_t t = bswap32(*(const uint32_t*)ptr);
                        if (t == FDT_BEGIN_NODE) return (int)(ptr - (const char*)fdt); // next subnode
                        if (t == FDT_END_NODE || t == FDT_END) return -1; // parent 結束了，沒 sibling
                        if (t == FDT_NOP) { ptr += 4; continue; }
                        return -1;
                    }
                }
                break;
            case FDT_PROP: {
                int prop_len = bswap32(*(const uint32_t*)ptr);
                ptr += 8 + prop_len;
                ptr = align_up(ptr, 4);
                break;
            }
            case FDT_NOP:
                break;
            case FDT_END:
            default:
                return -1;
        }
    }
}

unsigned long fdt_get_cpu_freq(const void* fdt) {
    int cpu_offset = fdt_path_offset(fdt, "/cpus");
    if (cpu_offset < 0) {
        return 0;
    }
    int ac, sc;
    fdt_get_cells(fdt, cpu_offset, &ac, &sc); // 根節點的 #address-cells / #size-cells 定義了 cpu node 中 reg property 的格式
    int len = 0;
    const void* prop = fdt_getprop(fdt, cpu_offset, "timebase-frequency", &len);
    if (!prop || len < 4) {
        return 0;
    }
    return fdt_read_cells((const uint32_t*)prop, ac); // timebase-frequency property 的值是一個 cell，cell 的大小由 #address-cells 定義，通常是 1 cell = 4 bytes
}

const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp) {
    struct fdt_header* header = (struct fdt_header*)fdt;
    if (bswap32(header->magic) != FDT_MAGIC) {
        return (void*)0;
    }
    const char* strings_base = (const char*)fdt + bswap32(header->off_dt_strings);
    const char* ptr = (const char*)fdt + nodeoffset;
    int current_depth = 0;
    while (1) {
        uint32_t token = bswap32(*(const uint32_t*)ptr);
        ptr += 4;
        switch (token) {
            case FDT_BEGIN_NODE:
                if (current_depth == 1) {
                    return (void*)0;
                }
                ptr += strlen(ptr) + 1; // skip the node name and the null terminator
                ptr = align_up(ptr, 4);
                current_depth++;
                break;
            case FDT_END_NODE:
                return (void*)0;
            case FDT_PROP: {
                int prop_len = bswap32(*(const uint32_t*)ptr);
                ptr += 4;
                int name_offset = bswap32(*(const uint32_t*)ptr);
                ptr += 4;
                if (strcmp(strings_base + name_offset, name) == 0) {
                    if (lenp) {
                        *lenp = prop_len;
                    }
                    return ptr;
                }
                ptr += prop_len;
                ptr = align_up(ptr, 4);
                break;
            }
            case FDT_NOP:
                break;
            case FDT_END:
                return (void*)0;
            default:
                return (void*)0;
        }
    }
    return (void*)0;
}
