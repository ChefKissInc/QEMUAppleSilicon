/*
 * Apple A7IOP Core.
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

#include "qemu/osdep.h"
#include "hw/misc/apple-silicon/a7iop/core.h"
#include "hw/misc/apple-silicon/a7iop/mailbox/core.h"
#include "hw/misc/apple-silicon/a7iop/private.h"
#include "qemu/bitops.h"
#include "qemu/lockable.h"

void apple_a7iop_send_ap(AppleA7IOP *s, AppleA7IOPMessage *msg)
{
    apple_a7iop_mailbox_send_ap(s->iop_mailbox, msg);
}

AppleA7IOPMessage *apple_a7iop_recv_ap(AppleA7IOP *s)
{
    return apple_a7iop_mailbox_recv_ap(s->iop_mailbox);
}

void apple_a7iop_send_iop(AppleA7IOP *s, AppleA7IOPMessage *msg)
{
    apple_a7iop_mailbox_send_iop(s->ap_mailbox, msg);
}

AppleA7IOPMessage *apple_a7iop_recv_iop(AppleA7IOP *s)
{
    return apple_a7iop_mailbox_recv_iop(s->ap_mailbox);
}

void apple_a7iop_cpu_start(AppleA7IOP *s, bool wake)
{
    // Already awake - do nothing.
    // Skip check if Secure Enclave Processor.
    if ((apple_a7iop_get_cpu_ctrl(s) & SEP_BOOT_MONITOR_RUN) == 0 &&
        (apple_a7iop_get_cpu_status(s) & CPU_STATUS_IDLE) == 0) {
        return;
    }

    apple_a7iop_set_cpu_status(s, apple_a7iop_get_cpu_status(s) &
                                      ~CPU_STATUS_IDLE);

    if (s->ops == NULL) {
        return;
    }

    if (wake) {
        if (s->ops->wakeup != NULL) {
            s->ops->wakeup(s);
        }
    } else if (s->ops->start != NULL) {
        s->ops->start(s);
    }
}

uint32_t apple_a7iop_get_cpu_status(AppleA7IOP *s)
{
    QEMU_LOCK_GUARD(&s->lock);
    return s->cpu_status;
}

void apple_a7iop_set_cpu_status(AppleA7IOP *s, uint32_t value)
{
    QEMU_LOCK_GUARD(&s->lock);
    s->cpu_status = value;
}

uint32_t apple_a7iop_get_cpu_ctrl(AppleA7IOP *s)
{
    QEMU_LOCK_GUARD(&s->lock);
    return s->cpu_ctrl;
}

void apple_a7iop_set_cpu_ctrl(AppleA7IOP *s, uint32_t value)
{
    WITH_QEMU_LOCK_GUARD(&s->lock)
    {
        s->cpu_ctrl = value;
    }
    if ((value & (CPU_CTRL_RUN | SEP_BOOT_MONITOR_RUN)) != 0) {
        apple_a7iop_cpu_start(s, false);
    }
}

void apple_a7iop_init(AppleA7IOP *s, const char *role, uint64_t mmio_size,
                      AppleA7IOPVersion version, const AppleA7IOPOps *ops,
                      QEMUBHFunc *handle_messages_func)
{
    DeviceState *dev;
    SysBusDevice *sbd;
    char name[32];

    dev = DEVICE(s);
    sbd = SYS_BUS_DEVICE(dev);

    s->role = g_strdup(role);
    s->ops = ops;

    qemu_mutex_init(&s->lock);

    snprintf(name, sizeof(name), "%s-iop", s->role);
    s->iop_mailbox = apple_a7iop_mailbox_new(name, version, NULL, NULL, s,
                                             handle_messages_func);
    object_property_add_child(OBJECT(dev), "iop-mailbox",
                              OBJECT(s->iop_mailbox));

    snprintf(name, sizeof(name), "%s-ap", s->role);
    s->ap_mailbox = apple_a7iop_mailbox_new(name, version, s->iop_mailbox, NULL,
                                            NULL, NULL);
    s->iop_mailbox->ap_mailbox = s->ap_mailbox;
    object_property_add_child(OBJECT(dev), "ap-mailbox", OBJECT(s->ap_mailbox));

    switch (version) {
    case APPLE_A7IOP_V2:
        apple_a7iop_init_mmio_v2(s, mmio_size);
        break;
    case APPLE_A7IOP_V4:
        apple_a7iop_init_mmio_v4(s, mmio_size);
        break;
    }

    sysbus_pass_irq(sbd, SYS_BUS_DEVICE(s->ap_mailbox));

    // qdev_init_gpio_out_named(dev, &s->iop_irq, APPLE_A7IOP_IOP_IRQ, 1);
}

static void apple_a7iop_reset(DeviceState *opaque)
{
    AppleA7IOP *s;

    s = APPLE_A7IOP(opaque);

    QEMU_LOCK_GUARD(&s->lock);
    s->cpu_status = CPU_STATUS_IDLE;
}

static void apple_a7iop_realize(DeviceState *opaque, Error **errp)
{
    AppleA7IOP *s;

    s = APPLE_A7IOP(opaque);

    sysbus_realize(SYS_BUS_DEVICE(s->iop_mailbox), errp);
    sysbus_realize(SYS_BUS_DEVICE(s->ap_mailbox), errp);
}

static void apple_a7iop_unrealize(DeviceState *opaque)
{
    AppleA7IOP *s;

    s = APPLE_A7IOP(opaque);

    qdev_unrealize(DEVICE(s->iop_mailbox));
    qdev_unrealize(DEVICE(s->ap_mailbox));
}

static void apple_a7iop_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc;

    dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, apple_a7iop_reset);
    dc->realize = apple_a7iop_realize;
    dc->unrealize = apple_a7iop_unrealize;
    dc->desc = "Apple A7IOP";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo apple_a7iop_info = {
    .name = TYPE_APPLE_A7IOP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AppleA7IOP),
    .class_init = apple_a7iop_class_init,
};

static void apple_a7iop_register_types(void)
{
    type_register_static(&apple_a7iop_info);
}

type_init(apple_a7iop_register_types);

const VMStateDescription vmstate_apple_a7iop = {
    .name = "AppleA7IOP",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32(cpu_status, AppleA7IOP),
            VMSTATE_UINT32(cpu_ctrl, AppleA7IOP),
            VMSTATE_END_OF_LIST(),
        },
};
