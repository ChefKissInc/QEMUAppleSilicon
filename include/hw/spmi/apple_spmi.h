/*
 * Apple System Management Power Interface.
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

#ifndef HW_SPMI_APPLE_SPMI_H
#define HW_SPMI_APPLE_SPMI_H

#include "qemu/osdep.h"
#include "hw/arm/apple-silicon/dt.h"
#include "hw/spmi/spmi.h"
#include "hw/sysbus.h"
#include "qemu/fifo32.h"
#include "qemu/queue.h"
#include "qom/object.h"

#define TYPE_APPLE_SPMI "apple-spmi"
OBJECT_DECLARE_TYPE(AppleSPMIState, AppleSPMIClass, APPLE_SPMI)
#define APPLE_SPMI_MMIO_SIZE (0x4000ULL)

typedef struct AppleSPMIClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;

    /*< public >*/
} AppleSPMIClass;

struct AppleSPMIState {
    SysBusDevice parent_obj;
    MemoryRegion container;
    MemoryRegion iomems[4];
    SPMIBus *bus;
    qemu_irq irq;
    qemu_irq resp_irq;
    Fifo32 resp_fifo;
    uint32_t control_reg[0x100 / sizeof(uint32_t)];
    uint32_t queue_reg[0x100 / sizeof(uint32_t)];
    uint32_t fault_reg[0x100 / sizeof(uint32_t)];
    uint32_t fault_counter_reg[0x64 / sizeof(uint32_t)];
    uint32_t resp_intr_index;
    uint32_t reg_vers;
    uint32_t *data;
    uint32_t data_length;
    uint32_t data_filled;
    uint32_t command;
};

SysBusDevice *apple_spmi_from_node(AppleDTNode *node);

#endif /* HW_SPMI_APPLE_SPMI_H */
