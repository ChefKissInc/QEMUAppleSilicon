/*
 * Apple I2C Controller.
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

#ifndef HW_I2C_APPLE_I2C_H
#define HW_I2C_APPLE_I2C_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/fifo8.h"
#include "qom/object.h"

#define TYPE_APPLE_I2C "apple-i2c"
OBJECT_DECLARE_TYPE(AppleI2CState, AppleHWI2CClass, APPLE_I2C)

#define APPLE_I2C_MMIO_SIZE (0x10000)
#define APPLE_I2C_SDA "i2c.sda"
#define APPLE_I2C_SCL "i2c.scl"

struct AppleHWI2CClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;

    /*< public >*/
};

struct AppleI2CState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;
    qemu_irq sda, scl;
    uint8_t reg[APPLE_I2C_MMIO_SIZE];
    Fifo8 rx_fifo;
    bool last_irq;
    bool nak;
    bool xip;
    bool is_recv;
};

SysBusDevice *apple_i2c_create(const char *name);
#endif /* HW_I2C_APPLE_I2C_H */
