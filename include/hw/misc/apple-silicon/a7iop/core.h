/*
 * Apple A7IOP Core.
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

#ifndef HW_MISC_APPLE_SILICON_A7IOP_CORE_H
#define HW_MISC_APPLE_SILICON_A7IOP_CORE_H

#include "qemu/osdep.h"
#include "hw/misc/apple-silicon/a7iop/base.h"
#include "hw/misc/apple-silicon/a7iop/mailbox/core.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"

#define CPU_CTRL_RUN BIT(4)
#define SEP_BOOT_MONITOR_RUN BIT(16)

#define TYPE_APPLE_A7IOP "apple-a7iop"
OBJECT_DECLARE_SIMPLE_TYPE(AppleA7IOP, APPLE_A7IOP)

typedef struct {
    void (*start)(AppleA7IOP *s);
    void (*wakeup)(AppleA7IOP *s);
} AppleA7IOPOps;

struct AppleA7IOP {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    const char *role;
    MemoryRegion mmio;
    const AppleA7IOPOps *ops;
    QemuMutex lock;
    AppleA7IOPMailbox *ap_mailbox;
    AppleA7IOPMailbox *iop_mailbox;
    uint32_t cpu_status;
    uint32_t cpu_ctrl;
};

void apple_a7iop_send_ap(AppleA7IOP *s, AppleA7IOPMessage *msg);
AppleA7IOPMessage *apple_a7iop_recv_ap(AppleA7IOP *s);
void apple_a7iop_send_iop(AppleA7IOP *s, AppleA7IOPMessage *msg);
AppleA7IOPMessage *apple_a7iop_recv_iop(AppleA7IOP *s);
void apple_a7iop_cpu_start(AppleA7IOP *s, bool wake);
uint32_t apple_a7iop_get_cpu_status(AppleA7IOP *s);
void apple_a7iop_set_cpu_status(AppleA7IOP *s, uint32_t value);
uint32_t apple_a7iop_get_cpu_ctrl(AppleA7IOP *s);
void apple_a7iop_set_cpu_ctrl(AppleA7IOP *s, uint32_t value);
void apple_a7iop_init(AppleA7IOP *s, const char *role, uint64_t mmio_size,
                      AppleA7IOPVersion version, const AppleA7IOPOps *ops,
                      QEMUBHFunc *handle_messages_func);

extern const VMStateDescription vmstate_apple_a7iop;

#define VMSTATE_APPLE_A7IOP(_field, _state) \
    VMSTATE_STRUCT(_field, _state, 0, vmstate_apple_a7iop, AppleA7IOP)

#endif /* HW_MISC_APPLE_SILICON_A7IOP_CORE_H */
