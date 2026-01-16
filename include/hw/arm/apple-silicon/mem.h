/*
 * Copyright (c) 2019 Jonathan Afek <jonyafek@me.com>
 * Copyright (c) 2025-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
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

#ifndef HW_ARM_APPLE_SILICON_MEM_H
#define HW_ARM_APPLE_SILICON_MEM_H

#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "exec/vaddr.h"
#include "hw/arm/apple-silicon/dt.h"

extern vaddr g_virt_base;
extern hwaddr g_phys_base;
extern vaddr g_virt_slide;
extern hwaddr g_phys_slide;

#define ROUND_UP_16K(v) ROUND_UP(v, 0x4000)

hwaddr vtop_static(vaddr va);
vaddr ptov_static(hwaddr pa);
hwaddr vtop_slid(vaddr va);
hwaddr vtop_mmu(vaddr va, CPUState *cs);

hwaddr vtop_bases(vaddr va, hwaddr phys_base, vaddr virt_base);
vaddr ptov_bases(hwaddr pa, hwaddr phys_base, vaddr virt_base);

void allocate_ram(MemoryRegion *top, const char *name, hwaddr addr, hwaddr size,
                  int priority);

typedef struct CarveoutAllocator CarveoutAllocator;

/// Creates a new carveout allocator
CarveoutAllocator *carveout_alloc_new(AppleDTNode *carveout_mmap,
                                      hwaddr dram_base, hwaddr dram_size,
                                      hwaddr alignment);
/// Returns the address of the allocated region.
hwaddr carveout_alloc_mem(CarveoutAllocator *ca, hwaddr size);
/// Returns the kernel region size.
/// The pointer `ca` will no longer be valid after this point.
hwaddr carveout_alloc_finalise(CarveoutAllocator *ca);

#endif /* HW_ARM_APPLE_SILICON_MEM_H */
