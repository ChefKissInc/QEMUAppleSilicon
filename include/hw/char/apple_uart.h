/*
 * Apple Samsung S5L UART Emulation
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

#ifndef HW_CHAR_APPLE_UART_H
#define HW_CHAR_APPLE_UART_H

#include "hw/or-irq.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "target/arm/cpu-qom.h"

DeviceState *apple_uart_create(hwaddr addr, int fifo_size, int channel,
                               Chardev *chr, qemu_irq irq);
#endif /* HW_CHAR_APPLE_UART_H */
