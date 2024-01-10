/*
 * Apple SEP.
 *
 * Copyright (c) 2023 Visual Ehrmanntraut.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_APPLE_SILICON_SEP_H
#define HW_ARM_APPLE_SILICON_SEP_H

#include "qemu/osdep.h"
#include "hw/arm/apple-silicon/a13.h"
#include "hw/arm/apple-silicon/dtb.h"
#include "hw/misc/apple-silicon/mailbox.h"
#include "hw/sysbus.h"
#include "qemu/typedefs.h"
#include "qom/object.h"

#define TYPE_APPLE_SEP "secure-enclave"
OBJECT_DECLARE_SIMPLE_TYPE(AppleSEPState, APPLE_SEP)

typedef struct {
    uint8_t key[32];
    uint64_t ecid;
    uint32_t config;
} AppleTRNGState;

struct AppleSEPState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    vaddr base;
    ARMCPU *cpu;
    bool modern;
    AppleMboxState *mbox;
    MemoryRegion *dma_mr;
    AddressSpace *dma_as;
    MemoryRegion trng_mr;
    MemoryRegion misc0_mr;
    MemoryRegion misc1_mr;
    MemoryRegion misc2_mr;
    AppleTRNGState trng_state;
    uint8_t misc0_regs[REG_SIZE];
    uint8_t misc1_regs[REG_SIZE];
    uint8_t misc2_regs[REG_SIZE];
};

AppleSEPState *apple_sep_create(DTBNode *node, vaddr base, uint32_t cpu_id,
                                uint32_t build_version, bool modern);

#endif /* HW_ARM_APPLE_SILICON_SEP_H */
