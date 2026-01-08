/*
 * Apple A7IOP Base.
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

#ifndef HW_MISC_APPLE_SILICON_A7IOP_BASE_H
#define HW_MISC_APPLE_SILICON_A7IOP_BASE_H

#include "qemu/osdep.h"

#define APPLE_A7IOP_SEP_CPU_IRQ "apple-a7iop-sep-cpu-irq"
#define APPLE_A7IOP_IOP_IRQ "apple-a7iop-iop-irq"
#define APPLE_A7IOP_SEP_GPIO_TIMER0 "a7iop-sep-gpio-timer0"
#define APPLE_A7IOP_SEP_GPIO_TIMER1 "a7iop-sep-gpio-timer1"

typedef enum {
    APPLE_A7IOP_V2 = 0,
    APPLE_A7IOP_V4,
} AppleA7IOPVersion;

enum {
    APPLE_A7IOP_IRQ_IOP_NONEMPTY = 0,
    APPLE_A7IOP_IRQ_IOP_EMPTY,
    APPLE_A7IOP_IRQ_AP_NONEMPTY,
    APPLE_A7IOP_IRQ_AP_EMPTY,
    APPLE_A7IOP_IRQ_MAX,
};

#endif /* HW_MISC_APPLE_SILICON_A7IOP_BASE_H */
