/*
 * Apple Roswell.
 *
 * Copyright (c) 2023-2024 Visual Ehrmanntraut.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_MISC_APPLE_SILICON_ROSWELL_H
#define HW_MISC_APPLE_SILICON_ROSWELL_H

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_APPLE_ROSWELL "apple-roswell"
OBJECT_DECLARE_SIMPLE_TYPE(AppleRoswellState, APPLE_ROSWELL);

struct AppleRoswellState {
    /*< private >*/
    I2CSlave i2c;

    /*< public >*/
};

void apple_roswell_create(MachineState *machine, uint8_t addr);

#endif /* HW_MISC_APPLE_SILICON_ROSWELL_H */
