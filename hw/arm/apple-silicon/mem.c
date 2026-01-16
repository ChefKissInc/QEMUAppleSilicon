/*
 * General Apple XNU memory utilities.
 *
 * Copyright (c) 2023-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "hw/arm/apple-silicon/dt.h"
#include "hw/arm/apple-silicon/mem.h"
#include "qapi/error.h"
#include "system/memory.h"

hwaddr g_virt_base, g_phys_base, g_virt_slide, g_phys_slide;

hwaddr vtop_bases(vaddr va, hwaddr phys_base, vaddr virt_base)
{
    g_assert_cmphex(phys_base, !=, 0);
    g_assert_cmphex(virt_base, !=, 0);

    return va - virt_base + phys_base;
}

vaddr ptov_bases(hwaddr pa, hwaddr phys_base, vaddr virt_base)
{
    g_assert_cmphex(phys_base, !=, 0);
    g_assert_cmphex(virt_base, !=, 0);

    return pa - phys_base + virt_base;
}

hwaddr vtop_static(vaddr va)
{
    return vtop_bases(va, g_phys_base, g_virt_base);
}

vaddr ptov_static(hwaddr pa)
{
    return ptov_bases(pa, g_phys_base, g_virt_base);
}

hwaddr vtop_slid(vaddr va)
{
    return vtop_static(va + g_virt_slide);
}

void allocate_ram(MemoryRegion *top, const char *name, hwaddr addr, hwaddr size,
                  int priority)
{
    MemoryRegion *sec = g_new(MemoryRegion, 1);
    memory_region_init_ram(sec, NULL, name, size, &error_fatal);
    memory_region_add_subregion_overlap(top, addr, sec, priority);
}

struct CarveoutAllocator {
    hwaddr dram_base;
    hwaddr end;
    hwaddr alignment;
    AppleDTNode *node;
    uint32_t cur_id;
};

CarveoutAllocator *carveout_alloc_new(AppleDTNode *carveout_mmap,
                                      hwaddr dram_base, hwaddr dram_size,
                                      hwaddr alignment)
{
    CarveoutAllocator *ca;

    g_assert_cmphex(dram_size, !=, 0);
    g_assert_cmphex(alignment, !=, 0);

    ca = g_new0(CarveoutAllocator, 1);
    ca->dram_base = dram_base;
    ca->end = dram_base + dram_size;
    ca->alignment = alignment;
    ca->node = carveout_mmap;

    return ca;
}

hwaddr carveout_alloc_mem(CarveoutAllocator *ca, hwaddr size)
{
    hwaddr data[2] = { 0 };
    char region_name[32] = { 0 };

    g_assert_cmphex(size, !=, 0);

    ca->end = ROUND_DOWN(ca->end - size, ca->alignment);

    data[0] = ca->end;
    data[1] = size;

    if (ca->node != NULL) {
        snprintf(region_name, sizeof(region_name), "region-id-%u", ca->cur_id);
        apple_dt_set_prop(ca->node, region_name, sizeof(data), data);

        ca->cur_id += 1;
        if (ca->cur_id == 55) { // This is an iBoot profiler region. SKIP!
            ca->cur_id += 1;
        }
    }

    return ca->end;
}

hwaddr carveout_alloc_finalise(CarveoutAllocator *ca)
{
    hwaddr ret;

    ret = ROUND_DOWN(ca->end - ca->dram_base, ca->alignment);

    g_free(ca);

    return ret;
}
