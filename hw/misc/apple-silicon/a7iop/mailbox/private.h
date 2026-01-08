/*
 * Apple A7IOP Mailbox Private.
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

#ifndef HW_MISC_APPLE_SILICON_A7IOP_MAILBOX_PRIVATE_H
#define HW_MISC_APPLE_SILICON_A7IOP_MAILBOX_PRIVATE_H

#include "hw/misc/apple-silicon/a7iop/mailbox/core.h"

uint32_t apple_a7iop_mailbox_get_int_mask(AppleA7IOPMailbox *s);
void apple_a7iop_mailbox_set_int_mask(AppleA7IOPMailbox *s, uint32_t value);
void apple_a7iop_mailbox_clear_int_mask(AppleA7IOPMailbox *s, uint32_t value);
uint32_t apple_a7iop_mailbox_get_iop_ctrl(AppleA7IOPMailbox *s);
void apple_a7iop_mailbox_set_iop_ctrl(AppleA7IOPMailbox *s, uint32_t value);
uint32_t apple_a7iop_mailbox_get_ap_ctrl(AppleA7IOPMailbox *s);
void apple_a7iop_mailbox_set_ap_ctrl(AppleA7IOPMailbox *s, uint32_t value);
void apple_a7iop_mailbox_init_mmio_v2(AppleA7IOPMailbox *s, const char *name);
void apple_a7iop_mailbox_init_mmio_v4(AppleA7IOPMailbox *s, const char *name);

#endif /* HW_MISC_APPLE_SILICON_A7IOP_MAILBOX_PRIVATE_H */
