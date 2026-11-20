#include "bootinfo.h"
#include "fdt.h"

const void   *g_fdt_base = (void *)0;
unsigned long g_mem_base = 0;
unsigned long g_mem_size = 0;

void bootinfo_init(const void *fdt)
{
    g_fdt_base = fdt;

    int root = fdt_root_offset(fdt);
    int mem  = fdt_path_offset(fdt, "/memory");
    if (root < 0 || mem < 0)
        return;

    unsigned long base = 0, size = 0;
    if (fdt_get_reg(fdt, root, mem, 0, &base, &size) < 0)
        return;

    g_mem_base = base;
    g_mem_size = size;
}
