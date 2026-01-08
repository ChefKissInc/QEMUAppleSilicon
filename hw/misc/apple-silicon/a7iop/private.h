/*
 * Apple A7IOP Private.
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

#ifndef HW_MISC_APPLE_SILICON_A7IOP_PRIVATE_H
#define HW_MISC_APPLE_SILICON_A7IOP_PRIVATE_H

#include "qemu/osdep.h"
#include "hw/misc/apple-silicon/a7iop/core.h"

#define CPU_STATUS_IDLE 0x1
#define AKF_STRIDE 0x4000

void apple_a7iop_init_mmio_v2(AppleA7IOP *s, uint64_t mmio_size);
void apple_a7iop_init_mmio_v4(AppleA7IOP *s, uint64_t mmio_size);

uint32_t apple_a7iop_interrupt_status_pop(AppleA7IOPMailbox *s);

#endif /* HW_MISC_APPLE_SILICON_A7IOP_PRIVATE_H */
