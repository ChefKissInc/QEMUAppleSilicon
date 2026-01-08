/*
 * Apple Interrupt Controller.
 *
 * Copyright (c) 2024-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
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

#ifndef HW_INTC_APPLE_AIC_H
#define HW_INTC_APPLE_AIC_H

#include "hw/arm/apple-silicon/dt.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_APPLE_AIC "apple-aic"
OBJECT_DECLARE_SIMPLE_TYPE(AppleAICState, APPLE_AIC)

typedef struct AppleAICState AppleAICState;

typedef struct {
    AppleAICState *aic;
    qemu_irq irq;
    MemoryRegion iomem;
    uint32_t cpu_id;
    uint32_t pendingIPI;
    uint32_t deferredIPI;
    uint32_t ipi_mask;
} AppleAICCPU;

struct AppleAICState {
    SysBusDevice parent_obj;
    QEMUTimer *timer;
    QemuMutex mutex;
    uint32_t phandle;
    uint32_t base_size;
    uint32_t numEIR;
    uint32_t numIRQ;
    uint32_t numCPU;
    uint32_t global_cfg;
    uint32_t time_base;
    uint32_t *eir_mask;
    uint32_t *eir_dest;
    AppleAICCPU *cpus;
    uint32_t *eir_state;
};


SysBusDevice *apple_aic_create(uint32_t numCPU, AppleDTNode *node,
                               AppleDTNode *timebase_node);

#endif /* HW_INTC_APPLE_AIC_H */
