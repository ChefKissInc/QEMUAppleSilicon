/*
 * Apple General-Purpose Input/Output.
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

#ifndef HW_GPIO_APPLE_GPIO_H
#define HW_GPIO_APPLE_GPIO_H

#include "hw/arm/apple-silicon/dt.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_APPLE_GPIO "apple-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(AppleGPIOState, APPLE_GPIO)

struct AppleGPIOState {
    SysBusDevice parent_obj;
    MemoryRegion *iomem;
    uint32_t pin_count;
    uint32_t irq_group_count;
    qemu_irq *irqs;
    qemu_irq *out;
    uint32_t *gpio_cfg;
    uint32_t int_config_len;
    uint32_t *int_config;
    uint32_t in_len;
    uint32_t *in;
    uint32_t *in_old;
    uint32_t npl;
};

DeviceState *apple_gpio_new(const char *name, uint64_t mmio_size,
                            uint32_t pin_count, uint32_t irq_group_count);
DeviceState *apple_gpio_from_node(AppleDTNode *node);
#endif
