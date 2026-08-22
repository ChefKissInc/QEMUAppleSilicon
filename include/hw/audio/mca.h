/*
 * Apple Multi-Channel Audio Controller.
 *
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

#pragma once

#include "qemu/osdep.h"
#include "hw/dma/apple_sio.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define APPLE_MCA_MMIO_SIO      (0)
#define APPLE_MCA_MMIO_DMA      (1)
#define APPLE_MCA_MMIO_MCLK_CFG (2)

#define TYPE_APPLE_MCA "apple.mca"
OBJECT_DECLARE_SIMPLE_TYPE(AppleMCAState, APPLE_MCA)

SysBusDevice* apple_mca_create(AppleDTNode* node, AppleSIODMAEndpoint* tx_ep, AppleSIODMAEndpoint* rx_ep);
