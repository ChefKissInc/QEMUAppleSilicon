/*
 * Apple Display Pipe V4 Controller.
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
#ifndef APPLE_DISPLAYPIPE_V4_H
#define APPLE_DISPLAYPIPE_V4_H

#include "qemu/osdep.h"
#include "hw/arm/apple-silicon/dt.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_APPLE_DISPLAY_PIPE_V4 "apple-display-pipe-v4"
OBJECT_DECLARE_SIMPLE_TYPE(AppleDisplayPipeV4State, APPLE_DISPLAY_PIPE_V4);

SysBusDevice *adp_v4_from_node(AppleDTNode *node, MemoryRegion *dma_mr);

void adp_v4_update_vram_mapping(AppleDisplayPipeV4State *s, MemoryRegion *mr,
                                hwaddr off, uint64_t size);

#endif /* APPLE_DISPLAYPIPE_V4_H */
