/*
 * Apple OTG Controller.
 *
 * Copyright (c) 2024-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
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

#ifndef HW_USB_APPLE_OTG_H
#define HW_USB_APPLE_OTG_H

#include "hw/arm/apple-silicon/dt.h"
#include "hw/sysbus.h"
#include "hw/usb/hcd-dwc2.h"
#include "qom/object.h"

#define TYPE_APPLE_OTG "apple-otg"
OBJECT_DECLARE_SIMPLE_TYPE(AppleOTGState, APPLE_OTG)

struct AppleOTGState {
    SysBusDevice parent_obj;
    MemoryRegion phy;
    uint8_t phy_reg[0x20];
    MemoryRegion usbctl;
    uint8_t usbctl_reg[0x1000];
    MemoryRegion widget;
    uint8_t widget_reg[0x100];
    MemoryRegion dwc2_mr;
    MemoryRegion dma_container_mr;
    MemoryRegion *dma_mr;
    DWC2State dwc2;
    uint64_t high_addr;
    SysBusDevice *host;
    char *fuzz_input;
    bool dart;
};

DeviceState *apple_otg_from_node(AppleDTNode *node);
#endif /* HW_USB_APPLE_OTG_H */
