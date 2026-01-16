/*
 * Apple RTKit.
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

#include "qemu/osdep.h"
#include "hw/misc/apple-silicon/a7iop/private.h"
#include "hw/misc/apple-silicon/a7iop/rtkit.h"
#include "qemu/lockable.h"
#include "trace.h"

#define EP_MANAGEMENT (0)
#define EP_CRASHLOG (1)
#define EP_USER_START (32)

#define EP0_IDLE (0)
#define EP0_WAIT_HELLO (1)
#define EP0_WAIT_ROLLCALL (2)
#define EP0_DONE (3)

#define MSG_HELLO (1)
#define MSG_HELLO_ACK (2)
#define MSG_TYPE_PING (3)
#define MSG_TYPE_PING_ACK (4)
#define MSG_TYPE_SET_EP_STATUS (5)
#define MSG_TYPE_REQ_POWER (6)
#define MSG_GET_PSTATE(_x) ((_x) & 0xFFF) // TODO: Investigate this
#define PSTATE_PWRGATE (0x0)
#define PSTATE_SLEEP (0x201)
#define PSTATE_OFF (0x202)
#define PSTATE_ON (0x220)
#define MSG_TYPE_POWER_ACK (7)
#define MSG_TYPE_ROLLCALL (8)
#define MSG_TYPE_POWER_NAP (10)
#define MSG_TYPE_AP_POWER (11)

typedef struct {
    uint64_t msg;
    uint32_t endpoint;
    uint32_t flags;
} QEMU_PACKED AppleRTKitMessage;

typedef union {
    union {
        struct {
            uint16_t min_version;
            uint16_t max_version;
        } hello;
        struct {
            uint16_t min_version;
            uint16_t max_version;
            union {
                struct {
                    uint32_t debug_en : 1;
                    uint32_t tracing_en : 1;
                };
                uint32_t raw;
            } flags;
        } hello_ack;
        struct {
            uint32_t seg;
            uint16_t timestamp;
        } ping;
        struct {
            uint32_t state;
            uint32_t ep;
        } epstart;
        struct {
            uint32_t state;
        } power;
        struct {
            uint32_t mask;
            uint32_t block : 6;
            uint32_t _rsvd : 13;
            uint32_t end : 1;
        } rollcall_v11;
        struct {
            uint64_t mask : 52;
        } rollcall_v10;
    };
    struct {
        uint64_t _rsvd : 52;
        uint64_t type : 4;
    };
    uint64_t raw;
} AppleRTKitManagementMessage;
QEMU_BUILD_BUG_ON(sizeof(AppleRTKitManagementMessage) != sizeof(uint64_t));

enum {
    RTKIT_MIN_VERSION = 3,
    RTKIT_MAX_VERSION_PRE_IOS14 = 10,
    RTKIT_MAX_VERSION = 12,
};

const VMStateDescription vmstate_apple_rtkit = {
    .name = "AppleRTKit",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT8(ep0_status, AppleRTKit),
            VMSTATE_UINT32(protocol_version, AppleRTKit),
            VMSTATE_APPLE_A7IOP_MESSAGE(rollcall, AppleRTKit),
            VMSTATE_END_OF_LIST(),
        },
};

static AppleA7IOPMessage *apple_rtkit_construct_msg(uint8_t ep, uint64_t data)
{
    AppleA7IOPMessage *msg;
    AppleRTKitMessage *rtk_msg;

    msg = g_new0(AppleA7IOPMessage, 1);
    rtk_msg = (AppleRTKitMessage *)msg->data;
    rtk_msg->endpoint = ep;
    rtk_msg->msg = data;

    return msg;
}

static void apple_rtkit_send_msg(AppleRTKit *s, uint8_t ep, uint64_t data)
{
    apple_a7iop_send_ap(APPLE_A7IOP(s), apple_rtkit_construct_msg(ep, data));
}

void apple_rtkit_send_control_msg(AppleRTKit *s, uint8_t ep, uint64_t data)
{
    g_assert_cmpuint(ep, <, EP_USER_START);
    apple_rtkit_send_msg(s, ep, data);
}

void apple_rtkit_send_user_msg(AppleRTKit *s, uint8_t ep, uint64_t data)
{
    g_assert_cmpuint(ep, <, 256 - EP_USER_START);
    apple_rtkit_send_msg(s, ep + EP_USER_START, data);
}

static void apple_rtkit_register_ep(AppleRTKit *s, uint8_t ep, void *opaque,
                                    AppleRTKitEPHandler *handler, bool user)
{
    AppleRTKitEPData *data;

    g_assert_false(g_hash_table_contains(s->endpoints, GUINT_TO_POINTER(ep)));

    data = g_new0(AppleRTKitEPData, 1);
    data->opaque = opaque;
    data->handler = handler;
    data->user = user;
    g_hash_table_insert(s->endpoints, GUINT_TO_POINTER(ep), data);
}

void apple_rtkit_register_control_ep(AppleRTKit *s, uint8_t ep, void *opaque,
                                     AppleRTKitEPHandler *handler)
{
    g_assert_cmpuint(ep, <, EP_USER_START);
    apple_rtkit_register_ep(s, ep, opaque, handler, false);
}

void apple_rtkit_register_user_ep(AppleRTKit *s, uint8_t ep, void *opaque,
                                  AppleRTKitEPHandler *handler)
{
    g_assert_cmpuint(ep, <, 256 - EP_USER_START);
    apple_rtkit_register_ep(s, ep + EP_USER_START, opaque, handler, true);
}

static void apple_rtkit_unregister_ep(AppleRTKit *s, uint8_t ep)
{
    g_hash_table_remove(s->endpoints, GUINT_TO_POINTER(ep));
}

void apple_rtkit_unregister_control_ep(AppleRTKit *s, uint8_t ep)
{
    g_assert_cmpuint(ep, <, EP_USER_START);
    apple_rtkit_unregister_ep(s, ep);
}

void apple_rtkit_unregister_user_ep(AppleRTKit *s, uint8_t ep)
{
    g_assert_cmpuint(ep, <, 256 - EP_USER_START);
    apple_rtkit_unregister_ep(s, ep + EP_USER_START);
}

static void apple_rtkit_mgmt_send_hello_msg(AppleRTKit *s, uint16_t min_version,
                                            uint16_t max_version)
{
    AppleRTKitManagementMessage msg = { 0 };

    trace_apple_rtkit_mgmt_send_hello(APPLE_A7IOP(s)->role);

    msg.type = MSG_HELLO;
    msg.hello.min_version = min_version;
    msg.hello.max_version = max_version;
    apple_rtkit_send_control_msg(s, EP_MANAGEMENT, msg.raw);
}

static void apple_rtkit_mgmt_send_hello(AppleRTKit *s)
{
    apple_rtkit_mgmt_send_hello_msg(s, RTKIT_MIN_VERSION, RTKIT_MAX_VERSION);
    s->ep0_status = EP0_WAIT_HELLO;
}

static void apple_rtkit_mgmt_rollcall_v10_foreach(gpointer key, gpointer value,
                                                  gpointer user_data)
{
    AppleRTKitManagementMessage *msg = user_data;
    msg->rollcall_v10.mask |= BIT_ULL(GPOINTER_TO_UINT(key));
}

static void apple_rtkit_mgmt_rollcall_v10(AppleRTKit *s)
{
    AppleRTKitManagementMessage msg = { 0 };

    msg.type = MSG_TYPE_ROLLCALL;
    g_hash_table_foreach(s->endpoints, apple_rtkit_mgmt_rollcall_v10_foreach,
                         &msg);
    apple_rtkit_send_control_msg(s, EP_MANAGEMENT, msg.raw);
}

typedef struct {
    AppleRTKit *s;
    uint32_t mask;
    uint32_t last_block;
} AppleRTKitManagementRollcallV11;

static void apple_rtkit_mgmt_rollcall_v11_foreach(gpointer key, gpointer value,
                                                  gpointer user_data)
{
    AppleRTKitManagementRollcallV11 *d = user_data;
    AppleRTKit *s = d->s;
    AppleRTKitManagementMessage mgmt_msg = { 0 };
    AppleA7IOPMessage *msg;

    uint32_t ep = GPOINTER_TO_UINT(key);

    if (ep / EP_USER_START != d->last_block && d->mask != 0) {
        mgmt_msg.type = MSG_TYPE_ROLLCALL;
        mgmt_msg.rollcall_v11.mask = d->mask;
        mgmt_msg.rollcall_v11.block = d->last_block;
        mgmt_msg.rollcall_v11.end = false;
        msg = apple_rtkit_construct_msg(EP_MANAGEMENT, mgmt_msg.raw);
        QTAILQ_INSERT_TAIL(&s->rollcall, msg, next);
        d->mask = 0;
    }

    d->last_block = ep / EP_USER_START;
    d->mask |= BIT(ep % EP_USER_START);
}

static void apple_rtkit_mgmt_rollcall_v11(AppleRTKit *s)
{
    AppleA7IOP *a7iop;
    AppleA7IOPMessage *msg;
    AppleRTKitManagementRollcallV11 data = { 0 };
    AppleRTKitManagementMessage mgmt_msg = { 0 };

    a7iop = APPLE_A7IOP(s);

    g_assert_true(QTAILQ_EMPTY(&s->rollcall));

    data.s = s;
    g_hash_table_foreach(s->endpoints, apple_rtkit_mgmt_rollcall_v11_foreach,
                         &data);
    mgmt_msg.type = MSG_TYPE_ROLLCALL;
    mgmt_msg.rollcall_v11.mask = data.mask;
    mgmt_msg.rollcall_v11.block = data.last_block;
    mgmt_msg.rollcall_v11.end = true;
    msg = apple_rtkit_construct_msg(EP_MANAGEMENT, mgmt_msg.raw);
    QTAILQ_INSERT_TAIL(&s->rollcall, msg, next);

    msg = QTAILQ_FIRST(&s->rollcall);
    QTAILQ_REMOVE(&s->rollcall, msg, next);
    apple_a7iop_send_ap(a7iop, msg);
}

static void apple_rtkit_mgmt_handle_msg(void *opaque, uint8_t ep,
                                        uint64_t message)
{
    AppleRTKit *s = opaque;
    AppleA7IOP *a7iop = opaque;
    const AppleRTKitManagementMessage *msg;
    AppleRTKitManagementMessage m = { 0 };

    msg = (const AppleRTKitManagementMessage *)&message;

    trace_apple_rtkit_handle_mgmt_msg(a7iop->role, msg->raw, s->ep0_status,
                                      msg->type);

    switch (msg->type) {
    case MSG_HELLO_ACK:
        g_assert_cmphex(s->ep0_status, ==, EP0_WAIT_HELLO);

        // We must resend hello on iOS <=13 with a lower version range.
        // FIXME: This doesn't work consistently;
        // iOS sometimes panics about the mismatch due to a race condition.
        // Good job Apple! Now we'll have to read the version from the fw.
        if (msg->hello_ack.min_version == 0xFFFF &&
            msg->hello_ack.max_version == 0xBAD) {
            apple_rtkit_mgmt_send_hello_msg(s, RTKIT_MIN_VERSION,
                                            RTKIT_MAX_VERSION_PRE_IOS14);
            break;
        }

        s->protocol_version = msg->hello_ack.max_version;

        if (s->protocol_version <= 10) {
            apple_rtkit_mgmt_rollcall_v10(s);
        } else {
            apple_rtkit_mgmt_rollcall_v11(s);
        }

        s->ep0_status = EP0_WAIT_ROLLCALL;
        break;
    case MSG_TYPE_PING:
        m.type = MSG_TYPE_PING_ACK;
        m.ping.seg = msg->ping.seg;
        m.ping.timestamp = msg->ping.timestamp;
        apple_rtkit_send_msg(s, ep, m.raw);
        break;
    case MSG_TYPE_AP_POWER:
        m.type = MSG_TYPE_AP_POWER;
        m.power.state = msg->power.state;
        apple_rtkit_send_msg(s, ep, m.raw);
        break;
    case MSG_TYPE_REQ_POWER:
        switch (MSG_GET_PSTATE(msg->raw)) {
        case PSTATE_ON:
            apple_a7iop_cpu_start(a7iop, true);
            break;
        case PSTATE_SLEEP:
        case PSTATE_OFF:
        case PSTATE_PWRGATE:
            apple_a7iop_set_cpu_status(
                a7iop, apple_a7iop_get_cpu_status(a7iop) | CPU_STATUS_IDLE);
            m.type = MSG_TYPE_POWER_ACK;
            m.power.state = MSG_GET_PSTATE(msg->raw);
            apple_rtkit_send_msg(s, ep, m.raw);
            break;
        default:
            break;
        }
        break;
    case MSG_TYPE_ROLLCALL:
        g_assert_cmphex(s->ep0_status, ==, EP0_WAIT_ROLLCALL);

        if (QTAILQ_EMPTY(&s->rollcall)) {
            m.type = MSG_TYPE_POWER_ACK;
            m.power.state = 32;
            s->ep0_status = EP0_IDLE;
            trace_apple_rtkit_rollcall_finished(a7iop->role);
            apple_rtkit_send_msg(s, ep, m.raw);

            if (s->ops != NULL && s->ops->boot_done != NULL) {
                s->ops->boot_done(s->opaque);
            }
        } else {
            AppleA7IOPMessage *m2 = QTAILQ_FIRST(&s->rollcall);
            QTAILQ_REMOVE(&s->rollcall, m2, next);
            apple_a7iop_send_ap(a7iop, m2);
        }
        break;
    default:
        break;
    }
}

static void apple_rtkit_iop_start(AppleA7IOP *s)
{
    AppleRTKit *rtk;

    rtk = APPLE_RTKIT(s);

    trace_apple_rtkit_iop_start(s->role);

    if (rtk->ops && rtk->ops->start) {
        rtk->ops->start(rtk->opaque);
    }

    apple_rtkit_mgmt_send_hello(rtk);
}

static void apple_rtkit_iop_wakeup(AppleA7IOP *s)
{
    AppleRTKit *rtk;

    rtk = APPLE_RTKIT(s);

    trace_apple_rtkit_iop_wakeup(s->role);

    if (rtk->ops && rtk->ops->wakeup) {
        rtk->ops->wakeup(rtk->opaque);
    }

    apple_rtkit_mgmt_send_hello(rtk);
}

static void apple_rtkit_handle_messages_bh(void *opaque)
{
    AppleRTKit *s = opaque;
    AppleA7IOP *a7iop = opaque;
    AppleRTKitEPData *data;
    AppleA7IOPMessage *msg;
    AppleRTKitMessage *rtk_msg;

    QEMU_LOCK_GUARD(&s->lock);

    while (!apple_a7iop_mailbox_is_empty(a7iop->iop_mailbox)) {
        msg = apple_a7iop_recv_iop(a7iop);
        rtk_msg = (AppleRTKitMessage *)msg->data;
        data = g_hash_table_lookup(s->endpoints,
                                   GUINT_TO_POINTER(rtk_msg->endpoint));
        if (data != NULL && data->handler != NULL) {
            data->handler(data->opaque,
                          data->user ? rtk_msg->endpoint - EP_USER_START :
                                       rtk_msg->endpoint,
                          rtk_msg->msg);
        }
        g_free(msg);
    }
}

static const AppleA7IOPOps apple_rtkit_iop_ops = {
    .start = apple_rtkit_iop_start,
    .wakeup = apple_rtkit_iop_wakeup,
};

void apple_rtkit_init(AppleRTKit *s, void *opaque, const char *role,
                      uint64_t mmio_size, AppleA7IOPVersion version,
                      const AppleRTKitOps *ops)
{
    AppleA7IOP *a7iop;

    a7iop = APPLE_A7IOP(s);
    apple_a7iop_init(a7iop, role, mmio_size, version, &apple_rtkit_iop_ops,
                     apple_rtkit_handle_messages_bh);

    s->opaque = opaque ? opaque : s;
    s->endpoints =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    s->ops = ops;
    QTAILQ_INIT(&s->rollcall);
    qemu_mutex_init(&s->lock);

    apple_rtkit_register_control_ep(s, EP_MANAGEMENT, s,
                                    apple_rtkit_mgmt_handle_msg);
    apple_rtkit_register_control_ep(s, EP_CRASHLOG, s, NULL);
}

AppleRTKit *apple_rtkit_new(void *opaque, const char *role, uint64_t mmio_size,
                            AppleA7IOPVersion version, const AppleRTKitOps *ops)
{
    AppleRTKit *s;

    s = APPLE_RTKIT(qdev_new(TYPE_APPLE_RTKIT));

    apple_rtkit_init(s, opaque, role, mmio_size, version, ops);

    return s;
}

static void apple_rtkit_reset_hold(Object *obj, ResetType type)
{
    AppleRTKit *s;
    AppleRTKitClass *rtkc;
    AppleA7IOPMessage *msg;
    AppleA7IOPMessage *msg_next;

    s = APPLE_RTKIT(obj);
    rtkc = APPLE_RTKIT_GET_CLASS(obj);

    if (rtkc->parent_phases.hold != NULL) {
        rtkc->parent_phases.hold(obj, type);
    }

    QEMU_LOCK_GUARD(&s->lock);

    s->ep0_status = EP0_IDLE;
    s->protocol_version = 0;

    QTAILQ_FOREACH_SAFE (msg, &s->rollcall, next, msg_next) {
        QTAILQ_REMOVE(&s->rollcall, msg, next);
        g_free(msg);
    }
}

static void apple_rtkit_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc;
    DeviceClass *dc;
    AppleRTKitClass *rtkc;

    rc = RESETTABLE_CLASS(klass);
    dc = DEVICE_CLASS(klass);
    rtkc = APPLE_RTKIT_CLASS(klass);

    dc->desc = "Apple RTKit IOP";
    resettable_class_set_parent_phases(rc, NULL, apple_rtkit_reset_hold, NULL,
                                       &rtkc->parent_phases);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo apple_rtkit_info = {
    .name = TYPE_APPLE_RTKIT,
    .parent = TYPE_APPLE_A7IOP,
    .instance_size = sizeof(AppleRTKit),
    .class_size = sizeof(AppleRTKitClass),
    .class_init = apple_rtkit_class_init,
};

static void apple_rtkit_register_types(void)
{
    type_register_static(&apple_rtkit_info);
}

type_init(apple_rtkit_register_types);
