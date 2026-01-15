/*
 * Apple A7IOP Mailbox.
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

#ifndef HW_MISC_APPLE_SILICON_A7IOP_MAILBOX_CORE_H
#define HW_MISC_APPLE_SILICON_A7IOP_MAILBOX_CORE_H

#include "qemu/osdep.h"
#include "block/aio.h"
#include "hw/misc/apple-silicon/a7iop/base.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/queue.h"

#define TYPE_APPLE_A7IOP_MAILBOX "apple-a7iop-mailbox"
OBJECT_DECLARE_SIMPLE_TYPE(AppleA7IOPMailbox, APPLE_A7IOP_MAILBOX)

#define IOP_EMPTY BIT(0)
#define IOP_NONEMPTY BIT(4)
#define AP_EMPTY BIT(8)
#define AP_NONEMPTY BIT(12)
// Applicable since t8020/A12.
// the following are guessed, no jailbreak available to verify
#define MAILBOX_MASKBIT_UNKN0_EMPTY BIT(16)
#define MAILBOX_MASKBIT_UNKN0_NONEMPTY BIT(18)
#define MAILBOX_MASKBIT_UNKN1_EMPTY BIT(20)
#define MAILBOX_MASKBIT_UNKN1_NONEMPTY BIT(22)
#define MAILBOX_MASKBIT_UNKN2_EMPTY BIT(24)
#define MAILBOX_MASKBIT_UNKN2_NONEMPTY BIT(26)
#define MAILBOX_MASKBIT_UNKN3_EMPTY BIT(28)
#define MAILBOX_MASKBIT_UNKN3_NONEMPTY BIT(30)

#define IRQ_IOP_NONEMPTY 0x40000
#define IRQ_IOP_EMPTY 0x40001
#define IRQ_AP_NONEMPTY 0x40002
#define IRQ_AP_EMPTY 0x40003
// Applicable since t8020/A12.
// the following are guessed, no jailbreak available to verify
#define IRQ_MAILBOX_UNKN0_NONEMPTY 0x40004
#define IRQ_MAILBOX_UNKN0_EMPTY 0x40005
#define IRQ_MAILBOX_UNKN1_NONEMPTY 0x40006
#define IRQ_MAILBOX_UNKN1_EMPTY 0x40007
#define IRQ_MAILBOX_UNKN2_NONEMPTY 0x40008
#define IRQ_MAILBOX_UNKN2_EMPTY 0x40009
#define IRQ_MAILBOX_UNKN3_NONEMPTY 0x4000A
#define IRQ_MAILBOX_UNKN3_EMPTY 0x4000B
// Timer0: phys, Timer1: virt (sepOS >= 16).
#define IRQ_SEP_TIMER0 0x70001
#define IRQ_SEP_TIMER1 0x70009
#define INTERRUPT_SEP_MANUAL_TIMER 0x10008

#define KIC_GLB_CFG_EXT_INT_EN BIT(0)
#define KIC_GLB_CFG_TIMEBASE_EN BIT(1)

#define REG_KIC_TMR_EN_MASK (BIT(3) | BIT(2) | BIT(1))
#define REG_KIC_TMR_INT_MASK_MASK BIT(1)

typedef struct AppleA7IOPMessage {
    uint8_t data[16];
    QTAILQ_ENTRY(AppleA7IOPMessage) next;
} AppleA7IOPMessage;

extern const VMStateDescription vmstate_apple_a7iop_message;

#define VMSTATE_APPLE_A7IOP_MESSAGE(_field, _state)                  \
    VMSTATE_QTAILQ_V(_field, _state, 0, vmstate_apple_a7iop_message, \
                     AppleA7IOPMessage, next)

typedef struct AppleA7IOPInterruptStatusMessage {
    uint32_t status;
    QTAILQ_ENTRY(AppleA7IOPInterruptStatusMessage) entry;
} AppleA7IOPInterruptStatusMessage;


struct AppleA7IOPMailbox {
    /*< private >*/
    SysBusDevice parent_obj;

    const char *role;
    QemuMutex lock;
    MemoryRegion mmio;
    QEMUBH *handle_messages_bh;
    QTAILQ_HEAD(, AppleA7IOPMessage) inbox;
    QTAILQ_HEAD(, AppleA7IOPInterruptStatusMessage) interrupt_status;
    uint32_t count;
    AppleA7IOPMailbox *iop_mailbox;
    AppleA7IOPMailbox *ap_mailbox;
    qemu_irq irqs[APPLE_A7IOP_IRQ_MAX];
    qemu_irq sep_cpu_irq;
    bool iop_dir_en;
    bool ap_dir_en;
    bool underflow;
    uint32_t int_mask;
    uint8_t iop_recv_reg[16];
    uint8_t ap_recv_reg[16];
    uint8_t iop_send_reg[16];
    uint8_t ap_send_reg[16];
    uint32_t interrupts_enabled[4];
    bool iop_nonempty;
    bool iop_empty;
    bool ap_nonempty;
    bool ap_empty;
    uint32_t glb_cfg;
    uint32_t timer0_enabled;
    uint32_t timer1_enabled;
    uint32_t timer0_masked;
    uint32_t timer1_masked;
};

void apple_a7iop_mailbox_update_irq_status(AppleA7IOPMailbox *s);
void apple_a7iop_mailbox_update_irq(AppleA7IOPMailbox *s);
bool apple_a7iop_mailbox_is_empty(AppleA7IOPMailbox *s);
void apple_a7iop_mailbox_send_ap(AppleA7IOPMailbox *s, AppleA7IOPMessage *msg);
void apple_a7iop_mailbox_send_iop(AppleA7IOPMailbox *s, AppleA7IOPMessage *msg);
uint32_t apple_a7iop_mailbox_read_interrupt_status(AppleA7IOPMailbox *s);
AppleA7IOPMessage *apple_a7iop_inbox_peek(AppleA7IOPMailbox *s);
void apple_a7iop_interrupt_status_push(AppleA7IOPMailbox *s, uint32_t status);
AppleA7IOPMessage *apple_a7iop_mailbox_recv_iop(AppleA7IOPMailbox *s);
AppleA7IOPMessage *apple_a7iop_mailbox_recv_ap(AppleA7IOPMailbox *s);
AppleA7IOPMailbox *apple_a7iop_mailbox_new(const char *role,
                                           AppleA7IOPVersion version,
                                           AppleA7IOPMailbox *iop_mailbox,
                                           AppleA7IOPMailbox *ap_mailbox,
                                           void *opaque,
                                           QEMUBHFunc *handle_messages_func);

#endif /* HW_MISC_APPLE_SILICON_A7IOP_MAILBOX_CORE_H */
