/*
 * Apple SPI Controller.
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

#ifndef HW_SSI_APPLE_SPI_H
#define HW_SSI_APPLE_SPI_H

#include "hw/arm/apple-silicon/dt.h"
#include "hw/dma/apple_sio.h"
#include "hw/sysbus.h"
#include "qemu/fifo32.h"
#include "qom/object.h"

#define APPLE_SPI_MMIO_SIZE (0x4000)

#define TYPE_APPLE_SPI "apple-spi"
OBJECT_DECLARE_SIMPLE_TYPE(AppleSPIState, APPLE_SPI)

struct AppleSPIState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SSIBus *ssi_bus;
    AppleSIODMAEndpoint *tx_chan;
    AppleSIODMAEndpoint *rx_chan;

    qemu_irq irq;
    qemu_irq cs_line;

    Fifo32 rx_fifo;
    Fifo32 tx_fifo;
    uint32_t regs[APPLE_SPI_MMIO_SIZE >> 2];

    int tx_chan_id;
    int rx_chan_id;
    bool dma_capable;
};

SysBusDevice *apple_spi_from_node(AppleDTNode *node);
SSIBus *apple_spi_get_bus(AppleSPIState *s);
#endif /* HW_SSI_APPLE_SPI_H */
