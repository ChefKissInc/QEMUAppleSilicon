/*
 * Apple NVMe MMU Controller.
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

#ifndef HW_BLOCK_APPLE_NVME_MMU_H
#define HW_BLOCK_APPLE_NVME_MMU_H

#include "hw/arm/apple-silicon/dt.h"
#include "hw/nvme/nvme.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
// #include "hw/pci/pci_device.h"
#include "hw/pci/pcie_host.h"
#include "hw/sysbus.h"

#define TYPE_APPLE_NVME_MMU "apple-nvme-mmu"
OBJECT_DECLARE_SIMPLE_TYPE(AppleNVMeMMUState, APPLE_NVME_MMU)

#define NVME_APPLE_MAX_PEND_CMDS 0x1210
#define NVME_APPLE_MAX_PEND_CMDS_VAL ((64 << 16) | 64)
#define NVME_APPLE_BOOT_STATUS 0x1300
#define NVME_APPLE_BOOT_STATUS_OK 0xde71ce55
#define NVME_APPLE_BASE_CMD_ID 0x1308
#define NVME_APPLE_BASE_CMD_ID_MASK 0xffff
#define NVME_APPLE_LINEAR_SQ_CTRL 0x24908
#define NVME_APPLE_LINEAR_SQ_CTRL_EN (1 << 0)
#define NVME_APPLE_MODESEL 0x1304
#define NVME_APPLE_VENDOR_REG_SIZE (0x60000)

struct AppleNVMeMMUState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomems[4];
    MemoryRegion io_mmio;
    MemoryRegion io_ioport;
    MemoryRegion io_ioport_for_alias;
    MemoryRegion bar0;
    qemu_irq irq;
    NvmeCtrl *nvme;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    PCIBus *pci_bus;
    uint32_t vendor_reg[NVME_APPLE_VENDOR_REG_SIZE / sizeof(uint32_t)];

    MemoryRegion common;
    uint32_t common_reg[0x4000 / sizeof(uint32_t)];
};

SysBusDevice *apple_nvme_mmu_from_node(AppleDTNode *node, PCIBus *pci_bus);

#endif /* HW_BLOCK_APPLE_NVME_MMU_H */
