/*
 * Apple A7IOP V4 Mailbox Registers.
 *
 * Copyright (c) 2023-2025 Visual Ehrmanntraut (VisualEhrmanntraut).
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
#include "qemu/lockable.h"
#include "qemu/log.h"
#include "private.h"

#if 0
#define IOP_LOG_MSG(s, t, msg)                                                \
    do {                                                                      \
        qemu_log_mask(LOG_GUEST_ERROR,                                        \
                      "%s: %s message (msg->endpoint: 0x%X "                  \
                      "msg->data[0]: 0x" HWADDR_FMT_plx                       \
                      " msg->data[1]: 0x" HWADDR_FMT_plx ")\n",               \
                      s->role, t, msg->endpoint, msg->data[0], msg->data[1]); \
    } while (0)
#else
#define IOP_LOG_MSG(s, t, msg) \
    do {                       \
    } while (0)
#endif


#define REG_INT_MASK_SET (0x0)
#define REG_INT_MASK_CLR (0x4)
#define REG_IOP_CTRL (0x8)
#define REG_AP_CTRL (0xC)
#define REG_IOP_SEND0 (0x700)
#define REG_IOP_SEND1 (0x704)
#define REG_IOP_SEND2 (0x708)
#define REG_IOP_SEND3 (0x70C)
#define REG_IOP_RECV0 (0x710)
#define REG_IOP_RECV1 (0x714)
#define REG_IOP_RECV2 (0x718)
#define REG_IOP_RECV3 (0x71C)
#define REG_AP_SEND0 (0x720)
#define REG_AP_SEND1 (0x724)
#define REG_AP_SEND2 (0x728)
#define REG_AP_SEND3 (0x72C)
#define REG_AP_RECV0 (0x730)
#define REG_AP_RECV1 (0x734)
#define REG_AP_RECV2 (0x738)
#define REG_AP_RECV3 (0x73C)

static void apple_a7iop_v4_mailbox_reg_write(void *opaque, hwaddr addr,
                                             const uint64_t data, unsigned size)
{
    AppleA7IOPMailbox *s = opaque;
    AppleA7IOPMessage *msg;

    switch (addr) {
    case REG_INT_MASK_SET:
        apple_a7iop_mailbox_set_int_mask(s, (uint32_t)data);
        break;
    case REG_INT_MASK_CLR:
        apple_a7iop_mailbox_clear_int_mask(s, (uint32_t)data);
        break;
    case REG_IOP_CTRL:
        apple_a7iop_mailbox_set_iop_ctrl(s, (uint32_t)data);
        break;
    case REG_AP_CTRL:
        apple_a7iop_mailbox_set_ap_ctrl(s, (uint32_t)data);
        break;
    case REG_IOP_SEND0:
    case REG_IOP_SEND1:
    case REG_IOP_SEND2:
    case REG_IOP_SEND3:
        qemu_mutex_lock(&s->lock);
        memcpy(s->iop_send_reg + (addr - REG_IOP_SEND0), &data, size);
        if (addr + size - 4 == REG_IOP_SEND3) {
            msg = g_new0(AppleA7IOPMessage, 1);
            memcpy(msg->data, s->iop_send_reg, sizeof(msg->data));
            qemu_mutex_unlock(&s->lock);
            apple_a7iop_mailbox_send_iop(s, msg);
            IOP_LOG_MSG(s, "AP sent", msg);
        } else {
            qemu_mutex_unlock(&s->lock);
        }
        break;
    case REG_AP_SEND0:
        QEMU_FALLTHROUGH;
    case REG_AP_SEND1:
        QEMU_FALLTHROUGH;
    case REG_AP_SEND2:
        QEMU_FALLTHROUGH;
    case REG_AP_SEND3:
        qemu_mutex_lock(&s->lock);
        memcpy(s->ap_send_reg + (addr - REG_AP_SEND0), &data, size);
        if (addr + size - 4 == REG_AP_SEND3) {
            msg = g_new0(AppleA7IOPMessage, 1);
            memcpy(msg->data, s->ap_send_reg, sizeof(msg->data));
            qemu_mutex_unlock(&s->lock);
            apple_a7iop_mailbox_send_ap(s, msg);
            IOP_LOG_MSG(s, "IOP sent", msg);
        } else {
            qemu_mutex_unlock(&s->lock);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s unknown @ 0x" HWADDR_FMT_plx " value 0x%" PRIx64 "\n",
                      __FUNCTION__, addr, data);
        break;
    }
}

static uint64_t apple_a7iop_v4_mailbox_reg_read(void *opaque, hwaddr addr,
                                                unsigned size)
{
    AppleA7IOPMailbox *s = opaque;
    AppleA7IOPMessage *msg;
    uint64_t ret = 0;

    switch (addr) {
    case REG_INT_MASK_SET:
        return apple_a7iop_mailbox_get_int_mask(s);
    case REG_INT_MASK_CLR:
        return apple_a7iop_mailbox_get_int_mask(s);
    case REG_IOP_CTRL:
        return apple_a7iop_mailbox_get_iop_ctrl(s);
    case REG_AP_CTRL:
        return apple_a7iop_mailbox_get_ap_ctrl(s);
    case REG_IOP_RECV0:
        WITH_QEMU_LOCK_GUARD(&s->lock)
        {
            msg = apple_a7iop_mailbox_recv_iop(s);
            if (msg == NULL) {
                memset(s->iop_recv_reg, 0, sizeof(s->iop_recv_reg));
            } else {
                memcpy(s->iop_recv_reg, msg->data, sizeof(s->iop_recv_reg));
                IOP_LOG_MSG(s, "IOP received", msg);
                g_free(msg);
            }
        }
        QEMU_FALLTHROUGH;
    case REG_IOP_RECV1:
    case REG_IOP_RECV2:
    case REG_IOP_RECV3:
        WITH_QEMU_LOCK_GUARD(&s->lock)
        {
            memcpy(&ret, s->iop_recv_reg + (addr - REG_IOP_RECV0), size);
        }
        break;
    case REG_AP_RECV0:
        WITH_QEMU_LOCK_GUARD(&s->lock)
        {
            msg = apple_a7iop_mailbox_recv_ap(s);
            if (msg == NULL) {
                memset(s->ap_recv_reg, 0, sizeof(s->ap_recv_reg));
            } else {
                memcpy(s->ap_recv_reg, msg->data, sizeof(s->ap_recv_reg));
                IOP_LOG_MSG(s, "AP received", msg);
                g_free(msg);
            }
        }
        QEMU_FALLTHROUGH;
    case REG_AP_RECV1:
    case REG_AP_RECV2:
    case REG_AP_RECV3:
        WITH_QEMU_LOCK_GUARD(&s->lock)
        {
            memcpy(&ret, s->ap_recv_reg + (addr - REG_AP_RECV0), size);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s unknown @ 0x" HWADDR_FMT_plx "\n",
                      __FUNCTION__, addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps apple_a7iop_v4_mailbox_reg_ops = {
    .write = apple_a7iop_v4_mailbox_reg_write,
    .read = apple_a7iop_v4_mailbox_reg_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size = 4,
    .impl.max_access_size = 8,
    .valid.unaligned = false,
};

void apple_a7iop_mailbox_init_mmio_v4(AppleA7IOPMailbox *s, const char *name)
{
    memory_region_init_io(&s->mmio, OBJECT(s), &apple_a7iop_v4_mailbox_reg_ops,
                          s, name, REG_AP_RECV3 + 4);
}
