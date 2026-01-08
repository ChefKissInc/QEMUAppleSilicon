/*
 * Apple SART.
 *
 * Copyright (c) 2023-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
 * Copyright (c) 2023-2026 Christian Inci (chris-pcguy).
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

#ifndef HW_ARM_APPLE_SILICON_SART_H
#define HW_ARM_APPLE_SILICON_SART_H

#include "qemu/osdep.h"
#include "hw/arm/apple-silicon/dt.h"
#include "hw/sysbus.h"
#include "qom/object.h"

typedef struct AppleSARTState AppleSARTState;

#define TYPE_APPLE_SART "apple-sart"
OBJECT_DECLARE_SIMPLE_TYPE(AppleSARTState, APPLE_SART)

#define TYPE_APPLE_SART_IOMMU_MEMORY_REGION "apple-sart-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(AppleSARTIOMMUMemoryRegion,
                           APPLE_SART_IOMMU_MEMORY_REGION)

SysBusDevice *apple_sart_from_node(AppleDTNode *node);

#endif /* HW_ARM_APPLE_SILICON_SART_H */
