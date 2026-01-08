/*
 * Apple Device Address Resolution Table.
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

#ifndef HW_ARM_APPLE_SILICON_DART_H
#define HW_ARM_APPLE_SILICON_DART_H

#include "qemu/osdep.h"
#include "hw/arm/apple-silicon/dt.h"
#include "qom/object.h"

typedef struct AppleDARTState AppleDARTState;

#define TYPE_APPLE_DART "apple-dart"
OBJECT_DECLARE_SIMPLE_TYPE(AppleDARTState, APPLE_DART)

#define TYPE_APPLE_DART_IOMMU_MEMORY_REGION "apple-dart-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(AppleDARTIOMMUMemoryRegion,
                           APPLE_DART_IOMMU_MEMORY_REGION)

#define DART_DART_FORCE_ACTIVE "dart-dart_force_active"
#define DART_DART_REQUEST_SID "dart-dart_request_sid"
#define DART_DART_RELEASE_SID "dart-dart_release_sid"
#define DART_DART_SELF "dart-dart_self"

IOMMUMemoryRegion *apple_dart_iommu_mr(AppleDARTState *dart, uint32_t sid);
IOMMUMemoryRegion *apple_dart_instance_iommu_mr(AppleDARTState *s,
                                                uint32_t instance,
                                                uint32_t sid);
AppleDARTState *apple_dart_from_node(AppleDTNode *node);

#endif /* HW_ARM_APPLE_SILICON_DART_H */
