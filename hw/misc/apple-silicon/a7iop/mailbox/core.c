/*
 * Apple A7IOP Mailbox.
 *
 * Copyright (c) 2023-2025 Visual Ehrmanntraut (VisualEhrmanntraut).
 * Copyright (c) 2023-2025 Christian Inci (chris-pcguy).
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

#include "qemu/osdep.h"
#include "block/aio.h"
#include "hw/irq.h"
#include "hw/misc/apple-silicon/a7iop/base.h"
#include "hw/misc/apple-silicon/a7iop/mailbox/core.h"
#include "hw/misc/apple-silicon/a7iop/mailbox/private.h"
#include "hw/misc/apple-silicon/a7iop/mailbox/trace.h"
#include "hw/misc/apple-silicon/a7iop/private.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/lockable.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/queue.h"

#define MAX_MESSAGE_COUNT (15)

#define CTRL_ENABLE BIT(0)
#define CTRL_FULL BIT(16)
#define CTRL_EMPTY BIT(17)
#define CTRL_OVERFLOW BIT(18)
#define CTRL_UNDERFLOW BIT(19)
#define CTRL_COUNT_SHIFT (20)
#define CTRL_COUNT_MASK (MAX_MESSAGE_COUNT << CTRL_COUNT_SHIFT)
#define CTRL_COUNT(v) (((v) << CTRL_COUNT_SHIFT) & CTRL_COUNT_MASK)

static bool is_interrupt_enabled(AppleA7IOPMailbox *s, uint32_t status)
{
    if ((status & 0xF0000) == 0x10000) {
        uint32_t interrupt = status & 0x7F;
        uint32_t interrupt_enabled =
            s->interrupts_enabled[interrupt / 32] & (1 << (interrupt % 32));
        if (interrupt_enabled) {
            return true;
        }
    } else if (status == IRQ_IOP_NONEMPTY) {
        if (s->iop_nonempty) {
            return true;
        }
    } else if (status == IRQ_IOP_EMPTY) {
        if (s->iop_empty) {
            return true;
        }
    } else if (status == IRQ_AP_NONEMPTY) {
        if (s->ap_nonempty) {
            return true;
        }
    } else if (status == IRQ_AP_EMPTY) {
        if (s->ap_empty) {
            return true;
        }
    } else if (status == IRQ_SEP_TIMER0) {
        if ((s->timer0_enabled & REG_KIC_TMR_EN_MASK) == REG_KIC_TMR_EN_MASK &&
            (s->timer0_masked & REG_KIC_TMR_INT_MASK_MASK) == 0) {
            return true;
        }
    } else if (status == IRQ_SEP_TIMER1) {
        if ((s->timer1_enabled & REG_KIC_TMR_EN_MASK) == REG_KIC_TMR_EN_MASK &&
            (s->timer1_masked & REG_KIC_TMR_INT_MASK_MASK) == 0) {
            return true;
        }
    } else {
        return true;
    }
    return false;
}


static bool apple_mbox_interrupt_status_empty(AppleA7IOPMailbox *s)
{
    AppleA7IOPInterruptStatusMessage *msg;

    QTAILQ_FOREACH (msg, &s->interrupt_status, entry) {
        if (is_interrupt_enabled(s, msg->status)) {
            return false;
        }
    }

    return true;
}

static inline bool iop_empty_is_unmasked(uint32_t int_mask)
{
    return (int_mask & IOP_EMPTY) == 0;
}

static inline bool iop_nonempty_is_unmasked(uint32_t int_mask)
{
    return (int_mask & IOP_NONEMPTY) == 0;
}

static inline bool ap_empty_is_unmasked(uint32_t int_mask)
{
    return (int_mask & AP_EMPTY) == 0;
}

static inline bool ap_nonempty_is_unmasked(uint32_t int_mask)
{
    return (int_mask & AP_NONEMPTY) == 0;
}

void apple_a7iop_mailbox_update_irq_status(AppleA7IOPMailbox *s)
{
    bool iop_empty;
    bool ap_empty;
    bool iop_underflow;
    bool ap_underflow;
    bool iop_nonempty_unmasked;
    bool iop_empty_unmasked;
    bool ap_nonempty_unmasked;
    bool ap_empty_unmasked;

    iop_empty = QTAILQ_EMPTY(&s->iop_mailbox->inbox);
    ap_empty = QTAILQ_EMPTY(&s->ap_mailbox->inbox);
    iop_underflow = s->iop_mailbox->underflow;
    ap_underflow = s->ap_mailbox->underflow;
    iop_nonempty_unmasked = iop_nonempty_is_unmasked(s->int_mask);
    iop_empty_unmasked = iop_empty_is_unmasked(s->int_mask);
    ap_nonempty_unmasked = ap_nonempty_is_unmasked(s->int_mask);
    ap_empty_unmasked = ap_empty_is_unmasked(s->int_mask);

    trace_apple_a7iop_mailbox_update_irq(
        s->role, iop_empty, ap_empty, !iop_nonempty_unmasked,
        !iop_empty_unmasked, !ap_nonempty_unmasked, !ap_empty_unmasked);

    qemu_set_irq(s->irqs[APPLE_A7IOP_IRQ_IOP_NONEMPTY],
                 (iop_nonempty_unmasked && !iop_empty) || iop_underflow);
    qemu_set_irq(s->irqs[APPLE_A7IOP_IRQ_IOP_EMPTY],
                 iop_empty_unmasked && iop_empty);

    qemu_set_irq(s->irqs[APPLE_A7IOP_IRQ_AP_NONEMPTY],
                 (ap_nonempty_unmasked && !ap_empty) || ap_underflow);
    qemu_set_irq(s->irqs[APPLE_A7IOP_IRQ_AP_EMPTY],
                 ap_empty_unmasked && ap_empty);

    s->iop_nonempty = (iop_nonempty_unmasked && !iop_empty) || iop_underflow;
    s->iop_empty = iop_empty_unmasked && iop_empty;
    s->ap_nonempty = (ap_nonempty_unmasked && !ap_empty) || ap_underflow;
    s->ap_empty = ap_empty_unmasked && ap_empty;
}

void apple_a7iop_mailbox_update_irq(AppleA7IOPMailbox *s)
{
    apple_a7iop_mailbox_update_irq_status(s);

    bool sep_cpu_irq_raised =
        s->iop_nonempty || s->iop_empty || s->ap_nonempty || s->ap_empty;
    sep_cpu_irq_raised |= !apple_mbox_interrupt_status_empty(s);
    if (!strcmp(s->role, "SEP-iop")) {
        qemu_set_irq(s->sep_cpu_irq, sep_cpu_irq_raised);
    }
    smp_mb();
    if (!strcmp(s->role, "SEP-ap")) {
        apple_a7iop_mailbox_update_irq(s->iop_mailbox);
    }
}

bool apple_a7iop_mailbox_is_empty(AppleA7IOPMailbox *s)
{
    QEMU_LOCK_GUARD(&s->lock);

    return s->underflow || QTAILQ_EMPTY(&s->inbox);
}

static void apple_a7iop_mailbox_send(AppleA7IOPMailbox *s,
                                     AppleA7IOPMessage *msg)
{
    g_assert_nonnull(msg);

    QEMU_LOCK_GUARD(&s->lock);

    trace_apple_a7iop_mailbox_send(s->role, ldq_le_p(msg->data),
                                   ldq_le_p(msg->data + sizeof(uint64_t)));

    QTAILQ_INSERT_TAIL(&s->inbox, msg, next);
    s->count++;
    apple_a7iop_mailbox_update_irq(s);

    if (s->handle_messages_bh != NULL) {
        qemu_bh_schedule(s->handle_messages_bh);
    }
}

void apple_a7iop_mailbox_send_ap(AppleA7IOPMailbox *s, AppleA7IOPMessage *msg)
{
    WITH_QEMU_LOCK_GUARD(&s->lock)
    {
        if (!s->ap_dir_en) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: %s direction not enabled.\n",
                          __FUNCTION__, s->role);
            return;
        }
    }

    apple_a7iop_mailbox_send(s->ap_mailbox, msg);
}

void apple_a7iop_mailbox_send_iop(AppleA7IOPMailbox *s, AppleA7IOPMessage *msg)
{
    WITH_QEMU_LOCK_GUARD(&s->lock)
    {
        if (!s->iop_dir_en) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: %s direction not enabled.\n",
                          __FUNCTION__, s->role);
            return;
        }
    }

    apple_a7iop_mailbox_send(s->iop_mailbox, msg);
}

AppleA7IOPMessage *apple_a7iop_inbox_peek(AppleA7IOPMailbox *s)
{
    return QTAILQ_FIRST(&s->inbox);
}

static AppleA7IOPMessage *apple_a7iop_mailbox_recv(AppleA7IOPMailbox *s)
{
    AppleA7IOPMessage *msg;

    if (s->underflow) {
        return NULL;
    }

    msg = QTAILQ_FIRST(&s->inbox);
    if (msg == NULL) {
        s->underflow = true;
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %s underflowed.\n", __FUNCTION__,
                      s->role);
        apple_a7iop_mailbox_update_irq(s);
        return NULL;
    }

    QTAILQ_REMOVE(&s->inbox, msg, next);
    stl_le_p(msg->data + 0xC, CTRL_COUNT(s->count));
    trace_apple_a7iop_mailbox_recv(s->role, ldq_le_p(msg->data),
                                   ldq_le_p(msg->data + sizeof(uint64_t)));
    s->count--;
    apple_a7iop_mailbox_update_irq(s);

    return msg;
}

AppleA7IOPMessage *apple_a7iop_mailbox_recv_iop(AppleA7IOPMailbox *s)
{
    AppleA7IOPMessage *msg;

    if (!s->iop_dir_en) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %s direction not enabled.\n",
                      __FUNCTION__, s->role);
        return NULL;
    }

    msg = apple_a7iop_mailbox_recv(s->iop_mailbox);

    return msg;
}

AppleA7IOPMessage *apple_a7iop_mailbox_recv_ap(AppleA7IOPMailbox *s)
{
    AppleA7IOPMessage *msg;

    if (!s->ap_dir_en) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %s direction not enabled.\n",
                      __FUNCTION__, s->role);
        return NULL;
    }

    msg = apple_a7iop_mailbox_recv(s->ap_mailbox);

    return msg;
}

uint32_t apple_a7iop_mailbox_get_int_mask(AppleA7IOPMailbox *s)
{
    QEMU_LOCK_GUARD(&s->lock);

    return s->int_mask;
}

void apple_a7iop_mailbox_set_int_mask(AppleA7IOPMailbox *s, uint32_t value)
{
    QEMU_LOCK_GUARD(&s->lock);

    s->int_mask |= value;
    apple_a7iop_mailbox_update_irq(s);
}

void apple_a7iop_mailbox_clear_int_mask(AppleA7IOPMailbox *s, uint32_t value)
{
    QEMU_LOCK_GUARD(&s->lock);

    s->int_mask &= ~value;
    apple_a7iop_mailbox_update_irq(s);
}

static inline uint32_t apple_a7iop_mailbox_ctrl(AppleA7IOPMailbox *s)
{
    if (s->underflow) {
        return CTRL_UNDERFLOW;
    }

    return (s->count >= MAX_MESSAGE_COUNT ? CTRL_FULL : 0) |
           (QTAILQ_EMPTY(&s->inbox) ? CTRL_EMPTY : 0) |
           CTRL_COUNT(MIN(s->count, MAX_MESSAGE_COUNT));
}

uint32_t apple_a7iop_mailbox_get_iop_ctrl(AppleA7IOPMailbox *s)
{
    QEMU_LOCK_GUARD(&s->lock);

    return (s->iop_dir_en ? CTRL_ENABLE : 0) |
           apple_a7iop_mailbox_ctrl(s->iop_mailbox);
}

void apple_a7iop_mailbox_set_iop_ctrl(AppleA7IOPMailbox *s, uint32_t value)
{
    QEMU_LOCK_GUARD(&s->lock);

    s->iop_dir_en = (value & CTRL_ENABLE) != 0;
}

uint32_t apple_a7iop_mailbox_get_ap_ctrl(AppleA7IOPMailbox *s)
{
    QEMU_LOCK_GUARD(&s->lock);

    return (s->ap_dir_en ? CTRL_ENABLE : 0) |
           apple_a7iop_mailbox_ctrl(s->ap_mailbox);
}

void apple_a7iop_mailbox_set_ap_ctrl(AppleA7IOPMailbox *s, uint32_t value)
{
    QEMU_LOCK_GUARD(&s->lock);

    s->ap_dir_en = (value & CTRL_ENABLE) != 0;
}

void apple_a7iop_interrupt_status_push(AppleA7IOPMailbox *s, uint32_t status)
{
    AppleA7IOPInterruptStatusMessage *msg;

    msg = g_new0(struct AppleA7IOPInterruptStatusMessage, 1);
    msg->status = status;
    QTAILQ_INSERT_TAIL(&s->interrupt_status, msg, entry);
    apple_a7iop_mailbox_update_irq(s);
}

uint32_t apple_a7iop_interrupt_status_pop(AppleA7IOPMailbox *s)
{
    uint32_t ret = 0;
    AppleA7IOPInterruptStatusMessage *msg, *preferred_msg = NULL;
    uint32_t interrupt_group_msg = 0, interrupt_group_preferred_msg = 0;

    // order should be 0x4, 0x7, 0x1, but 0x4 shouldn't be here at all.

    QTAILQ_FOREACH (msg, &s->interrupt_status, entry) {
        interrupt_group_msg = (msg->status >> 16) & 0x7;
        if (is_interrupt_enabled(s, msg->status)) {
            if (preferred_msg == NULL ||
                (interrupt_group_msg == interrupt_group_preferred_msg &&
                 msg->status < preferred_msg->status) ||
                (interrupt_group_msg != interrupt_group_preferred_msg &&
                 interrupt_group_msg == 0x7)) {
                preferred_msg = msg;
                interrupt_group_preferred_msg = interrupt_group_msg;
            }
        }
    }

    if (preferred_msg) {
        QTAILQ_REMOVE(&s->interrupt_status, preferred_msg, entry);
        ret = preferred_msg->status;
        g_free(preferred_msg);
    }

    apple_a7iop_mailbox_update_irq(s);

    return ret;
}

uint32_t apple_a7iop_mailbox_read_interrupt_status(AppleA7IOPMailbox *s)
{
    QEMU_LOCK_GUARD(&s->lock);
    // the order should be: 0x4..., 0x7..., 0x1...
    AppleA7IOPMailbox *a7iop_mbox = s->iop_mailbox;
    uint32_t interrupt_status;
    if (a7iop_mbox->iop_nonempty) {
        interrupt_status = IRQ_IOP_NONEMPTY;
        a7iop_mbox->int_mask |= IOP_NONEMPTY;
    } else if (a7iop_mbox->iop_empty) {
        interrupt_status = IRQ_IOP_EMPTY;
        a7iop_mbox->int_mask |= IOP_EMPTY;
    } else if (a7iop_mbox->ap_nonempty) {
        interrupt_status = IRQ_AP_NONEMPTY;
        a7iop_mbox->int_mask |= AP_NONEMPTY;
    } else if (a7iop_mbox->ap_empty) {
        interrupt_status = IRQ_AP_EMPTY;
        a7iop_mbox->int_mask |= AP_EMPTY;
    } else if ((interrupt_status = apple_a7iop_interrupt_status_pop(s)) != 0) {
        if ((interrupt_status & 0xf0000) == 0x10000) {
            int interrupt_group = (interrupt_status >> 6) & 0x3ff;
            g_assert_cmpuint(interrupt_group, <, 4);
            s->iop_mailbox->interrupts_enabled[interrupt_group] |=
                (interrupt_status & 0xf);
        } else if (interrupt_status == IRQ_IOP_NONEMPTY) {
            a7iop_mbox->int_mask |= IOP_NONEMPTY;
        } else if (interrupt_status == IRQ_IOP_EMPTY) {
            a7iop_mbox->int_mask |= IOP_EMPTY;
        } else if (interrupt_status == IRQ_AP_NONEMPTY) {
            a7iop_mbox->int_mask |= AP_NONEMPTY;
        } else if (interrupt_status == IRQ_AP_EMPTY) {
            a7iop_mbox->int_mask |= AP_EMPTY;
        } else if (interrupt_status == IRQ_MAILBOX_UNKN0_NONEMPTY) {
            a7iop_mbox->int_mask |= MAILBOX_MASKBIT_UNKN0_NONEMPTY;
        } else if (interrupt_status == IRQ_MAILBOX_UNKN0_EMPTY) {
            a7iop_mbox->int_mask |= MAILBOX_MASKBIT_UNKN0_EMPTY;
        } else if (interrupt_status == IRQ_MAILBOX_UNKN1_NONEMPTY) {
            a7iop_mbox->int_mask |= MAILBOX_MASKBIT_UNKN1_NONEMPTY;
        } else if (interrupt_status == IRQ_MAILBOX_UNKN1_EMPTY) {
            a7iop_mbox->int_mask |= MAILBOX_MASKBIT_UNKN1_EMPTY;
        } else if (interrupt_status == IRQ_MAILBOX_UNKN2_NONEMPTY) {
            a7iop_mbox->int_mask |= MAILBOX_MASKBIT_UNKN2_NONEMPTY;
        } else if (interrupt_status == IRQ_MAILBOX_UNKN2_EMPTY) {
            a7iop_mbox->int_mask |= MAILBOX_MASKBIT_UNKN2_EMPTY;
        } else if (interrupt_status == IRQ_MAILBOX_UNKN3_NONEMPTY) {
            a7iop_mbox->int_mask |= MAILBOX_MASKBIT_UNKN3_NONEMPTY;
        } else if (interrupt_status == IRQ_MAILBOX_UNKN3_EMPTY) {
            a7iop_mbox->int_mask |= MAILBOX_MASKBIT_UNKN3_EMPTY;
        } else if (interrupt_status == IRQ_SEP_TIMER0) {
            a7iop_mbox->timer0_masked |= REG_KIC_TMR_INT_MASK_MASK;
        } else if (interrupt_status == IRQ_SEP_TIMER1) {
            a7iop_mbox->timer1_masked |= REG_KIC_TMR_INT_MASK_MASK;
        }
    }
    apple_a7iop_mailbox_update_irq(s);
    return interrupt_status;
}

static void apple_a7iop_gpio_timer0(void *opaque, int n, int level)
{
    AppleA7IOPMailbox *s = opaque;
    bool val = !!level;
    assert(n == 0);
    if (!val) {
        return;
    }
    QEMU_LOCK_GUARD(&s->lock);
    if ((s->timer0_enabled & REG_KIC_TMR_EN_MASK) == REG_KIC_TMR_EN_MASK &&
        (s->timer0_masked & REG_KIC_TMR_INT_MASK_MASK) == 0) {
        apple_a7iop_interrupt_status_push(s, IRQ_SEP_TIMER0);
    }
}

static void apple_a7iop_gpio_timer1(void *opaque, int n, int level)
{
    AppleA7IOPMailbox *s = opaque;
    bool val = !!level;
    assert(n == 0);
    if (!val) {
        return;
    }
    QEMU_LOCK_GUARD(&s->lock);
    if ((s->timer1_enabled & REG_KIC_TMR_EN_MASK) == REG_KIC_TMR_EN_MASK &&
        (s->timer1_masked & REG_KIC_TMR_INT_MASK_MASK) == 0) {
        apple_a7iop_interrupt_status_push(s, IRQ_SEP_TIMER1);
    }
}

AppleA7IOPMailbox *apple_a7iop_mailbox_new(const char *role,
                                           AppleA7IOPVersion version,
                                           AppleA7IOPMailbox *iop_mailbox,
                                           AppleA7IOPMailbox *ap_mailbox,
                                           void *opaque,
                                           QEMUBHFunc *handle_messages_func)
{
    DeviceState *dev;
    SysBusDevice *sbd;
    AppleA7IOPMailbox *s;
    int i;
    char name[128] = { 0 };

    dev = qdev_new(TYPE_APPLE_A7IOP_MAILBOX);
    sbd = SYS_BUS_DEVICE(dev);
    s = APPLE_A7IOP_MAILBOX(dev);

    s->role = g_strdup(role);
    s->iop_mailbox = iop_mailbox ? iop_mailbox : s;
    s->ap_mailbox = ap_mailbox ? ap_mailbox : s;
    if (handle_messages_func != NULL) {
        s->handle_messages_bh =
            aio_bh_new_guarded(qemu_get_aio_context(), handle_messages_func,
                               opaque, &dev->mem_reentrancy_guard);
    }
    QTAILQ_INIT(&s->inbox);
    QTAILQ_INIT(&s->interrupt_status);
    qemu_mutex_init(&s->lock);
    for (i = 0; i < APPLE_A7IOP_IRQ_MAX; i++) {
        sysbus_init_irq(sbd, s->irqs + i);
    }
    qdev_init_gpio_out_named(dev, &s->sep_cpu_irq, APPLE_A7IOP_SEP_CPU_IRQ, 1);

    snprintf(name, sizeof(name), TYPE_APPLE_A7IOP_MAILBOX ".%s.regs", s->role);

    switch (version) {
    case APPLE_A7IOP_V2:
        apple_a7iop_mailbox_init_mmio_v2(s, name);
        break;
    case APPLE_A7IOP_V4:
        apple_a7iop_mailbox_init_mmio_v4(s, name);
        break;
    }

    sysbus_init_mmio(sbd, &s->mmio);

    if (!strcmp(s->role, "SEP-iop")) {
        qdev_init_gpio_in_named(DEVICE(s), apple_a7iop_gpio_timer0,
                                APPLE_A7IOP_SEP_GPIO_TIMER0, 1);
        qdev_init_gpio_in_named(DEVICE(s), apple_a7iop_gpio_timer1,
                                APPLE_A7IOP_SEP_GPIO_TIMER1, 1);
    }

    return s;
}

static void apple_a7iop_mailbox_reset(DeviceState *dev)
{
    AppleA7IOPMailbox *s;
    AppleA7IOPMessage *msg;

    s = APPLE_A7IOP_MAILBOX(dev);

    QEMU_LOCK_GUARD(&s->lock);

    g_assert_false(s->iop_mailbox == s->ap_mailbox);

    s->count = 0;
    s->iop_dir_en = true;
    s->ap_dir_en = true;
    s->underflow = false;
    memset(s->iop_recv_reg, 0, sizeof(s->iop_recv_reg));
    memset(s->ap_recv_reg, 0, sizeof(s->ap_recv_reg));
    memset(s->iop_send_reg, 0, sizeof(s->iop_send_reg));
    memset(s->ap_send_reg, 0, sizeof(s->ap_send_reg));

    while (!QTAILQ_EMPTY(&s->inbox)) {
        msg = QTAILQ_FIRST(&s->inbox);
        QTAILQ_REMOVE(&s->inbox, msg, next);
        g_free(msg);
    }
    while (!QTAILQ_EMPTY(&s->interrupt_status)) {
        AppleA7IOPInterruptStatusMessage *m =
            QTAILQ_FIRST(&s->interrupt_status);
        QTAILQ_REMOVE(&s->interrupt_status, m, entry);
        g_free(m);
    }
    for (int i = 0; i < 4; i++) {
        s->interrupts_enabled[i] = 0;
    }
    s->iop_nonempty = 0;
    s->iop_empty = 0;
    s->ap_nonempty = 0;
    s->ap_empty = 0;
    s->timer0_enabled = 0;
    s->timer1_enabled = 0;
    s->timer0_masked = 0;
    s->timer1_masked = 0;
    apple_a7iop_mailbox_update_irq_status(s);
}

const VMStateDescription vmstate_apple_a7iop_message = {
    .name = "Apple A7IOP Message State",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT8_ARRAY(data, AppleA7IOPMessage, 16),
            VMSTATE_END_OF_LIST(),
        }
};

static const VMStateDescription vmstate_apple_a7iop_int_status_message = {
    .name = "Apple A7IOP Interrupt Status Message State",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32(status, AppleA7IOPInterruptStatusMessage),
            VMSTATE_END_OF_LIST(),
        }
};

static const VMStateDescription vmstate_apple_a7iop_mailbox = {
    .name = "Apple A7IOP Mailbox State",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_APPLE_A7IOP_MESSAGE(inbox, AppleA7IOPMailbox),
            VMSTATE_QTAILQ_V(interrupt_status, AppleA7IOPMailbox, 0,
                             vmstate_apple_a7iop_int_status_message,
                             AppleA7IOPInterruptStatusMessage, entry),
            VMSTATE_UINT32(count, AppleA7IOPMailbox),
            VMSTATE_BOOL(iop_dir_en, AppleA7IOPMailbox),
            VMSTATE_BOOL(ap_dir_en, AppleA7IOPMailbox),
            VMSTATE_BOOL(underflow, AppleA7IOPMailbox),
            VMSTATE_UINT32(int_mask, AppleA7IOPMailbox),
            VMSTATE_UINT8_ARRAY(iop_recv_reg, AppleA7IOPMailbox, 16),
            VMSTATE_UINT8_ARRAY(ap_recv_reg, AppleA7IOPMailbox, 16),
            VMSTATE_UINT8_ARRAY(iop_send_reg, AppleA7IOPMailbox, 16),
            VMSTATE_UINT8_ARRAY(ap_send_reg, AppleA7IOPMailbox, 16),
            VMSTATE_UINT32_ARRAY(interrupts_enabled, AppleA7IOPMailbox, 4),
            VMSTATE_BOOL(iop_nonempty, AppleA7IOPMailbox),
            VMSTATE_BOOL(iop_empty, AppleA7IOPMailbox),
            VMSTATE_BOOL(ap_nonempty, AppleA7IOPMailbox),
            VMSTATE_BOOL(ap_empty, AppleA7IOPMailbox),
            VMSTATE_UINT32(timer0_enabled, AppleA7IOPMailbox),
            VMSTATE_UINT32(timer1_enabled, AppleA7IOPMailbox),
            VMSTATE_UINT32(timer0_masked, AppleA7IOPMailbox),
            VMSTATE_UINT32(timer1_masked, AppleA7IOPMailbox),
            VMSTATE_END_OF_LIST(),
        }
};

static void apple_a7iop_mailbox_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc;

    dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_apple_a7iop_mailbox;
    device_class_set_legacy_reset(dc, apple_a7iop_mailbox_reset);
    dc->desc = "Apple A7IOP Mailbox";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo apple_a7iop_mailbox_info = {
    .name = TYPE_APPLE_A7IOP_MAILBOX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AppleA7IOPMailbox),
    .class_init = apple_a7iop_mailbox_class_init,
};

static void apple_a7iop_mailbox_register_types(void)
{
    type_register_static(&apple_a7iop_mailbox_info);
}

type_init(apple_a7iop_mailbox_register_types);
