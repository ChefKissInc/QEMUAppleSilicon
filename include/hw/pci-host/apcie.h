/*
 * Apple PCIe IP block emulation
 * Frankenstein's monster built from gutted designware/xiling
 *
 * Copyright (c) 2025-2026 Christian Inci (chris-pcguy).
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

#ifndef APCIE_H
#define APCIE_H

#include "hw/arm/apple-silicon/dt.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_APPLE_PCIE_ROOT_BUS "apple-pcie-root-BUS"
OBJECT_DECLARE_SIMPLE_TYPE(ApplePCIERootBus, APPLE_PCIE_ROOT_BUS)

#define TYPE_APPLE_PCIE_PORT "apple-pcie-port"
OBJECT_DECLARE_SIMPLE_TYPE(ApplePCIEPort, APPLE_PCIE_PORT)

#define TYPE_APPLE_PCIE_HOST "apple-pcie-host"
OBJECT_DECLARE_SIMPLE_TYPE(ApplePCIEHost, APPLE_PCIE_HOST)

#define TYPE_APPLE_PCIE "apple-pcie"
OBJECT_DECLARE_SIMPLE_TYPE(ApplePCIEState, APPLE_PCIE)

// sizes: s8000 == 0x8000 ; t8030 == 0x4000 ; t8015 == 0x50000
// #define APCIE_COMMON_REGS_LENGTH 0x8000
#define APCIE_COMMON_REGS_LENGTH 0x50000

#define APCIE_ROOT_COMMON_ADDRESS 0x600000000ULL

#define APCIE_MAX_PORTS 4

#define APCIE_PORT_GPIO_CLKREQ_OUT "apcie-port-gpio-clkreq-out"
#define APCIE_PORT_GPIO_PERST "apcie-port-gpio-perst"

struct ApplePCIERootBus {
    PCIBus parent;
};

#if 1
typedef struct ApplePCIEMSIBank {
    uint32_t enable;
    uint32_t mask;
    uint32_t status;
} ApplePCIEMSIBank;

typedef struct ApplePCIEMSI {
    uint64_t base;
    MemoryRegion iomem;

#define APPLE_PCIE_NUM_MSI_BANKS 1
    // #define APPLE_PCIE_NUM_MSI_BANKS 8

    ApplePCIEMSIBank intr[APPLE_PCIE_NUM_MSI_BANKS];
} ApplePCIEMSI;
#endif

struct ApplePCIEHost {
    PCIExpressHost parent_obj;

    ApplePCIEState *pcie;

    MemoryRegion mmio, io;
    qemu_irq irqs[4];
    qemu_irq msi_irqs[8 * APCIE_MAX_PORTS];
    // uint32_t clkreq_gpio_id;
    // uint32_t clkreq_gpio_value;

    MemoryRegion root_cfg;
    MemoryRegion root_common;
    MemoryRegion root_phy;
    MemoryRegion root_phy_ip;
    MemoryRegion root_axi2af;
    uint32_t root_phy_enabled;
    uint32_t root_refclk_buffer_enabled;
    uint32_t root_common_regs[APCIE_COMMON_REGS_LENGTH / sizeof(uint32_t)];
};

struct ApplePCIEPort {
    PCIESlot parent_obj;

    // char bus_path[8];
    // char name[16];

    uint32_t bus_nr;
    uint32_t device_id;
    uint32_t manual_enable;
    uint32_t maximum_link_speed;

    ApplePCIEHost *host;
#if 1
    // qemu_irq msi_irqs[8];
    ApplePCIEMSI msi;
#endif
    MemoryRegion *dma_mr;
    AddressSpace dma_as;

    MemoryRegion port_cfg;
    MemoryRegion port_phy_glue;
    MemoryRegion port_phy_ip;
    MemoryRegion port_config_ltssm_debug;

    uint32_t port_ltssm_enable; // 0x80
    uint32_t port_pme_to_ack; // 0x8c
    uint32_t port_last_interrupt; // 0x100
    uint32_t port_interrupt_mask; // 0x104
    uint32_t port_hotreset; // 0x13c
    uint32_t port_cfg_port_config; // 0x800
    uint32_t port_cfg_refclk_config; // 0x810
    uint32_t port_cfg_rootport_perst; // 0x814
    uint32_t port_refclk_buffer_enabled;
    uint32_t port_msiVectors; // 0x124
    uint32_t port_msiUnknown0; // 0x128
    uint32_t port_linkcdmsts; // 0x210
    uint32_t port_rid_sid_map[0x40]; // 0x828 .. 0x924

    uint32_t port_ltssm_status; // 0x30

    qemu_irq apcie_port_gpio_clkreq_irq;
    bool gpio_perst_val;
    bool gpio_clkreq_val;
    bool skip_reset_clear;
    bool is_link_up;
    bool is_link_in_l2;
};

struct ApplePCIEState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    AppleDTNode *node;

    ApplePCIEHost *host;
    ApplePCIEPort *ports[APCIE_MAX_PORTS];
    uint32_t chip_id;
    uint32_t msi_vector_offset;
};

void port_devices_set_power(ApplePCIEPort *port, bool power);
void apple_pcie_port_temp_lower_msi_irq(ApplePCIEPort *port,
                                        int msi_intr_index);
SysBusDevice *apple_pcie_from_node(AppleDTNode *node, uint32_t chip_id);

#endif /* APCIE_H */
