/*
 * Apple A7IOP V2 Mailbox Registers.
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
#include "qemu/lockable.h"
#include "qemu/log.h"
#include "private.h"

#define REG_INT_MASK_SET 0x00
#define REG_INT_MASK_CLR 0x04
#define REG_IOP_CTRL     0x08
#define REG_IOP_SEND0    0x10
#define REG_IOP_SEND1    0x14
#define REG_IOP_RECV0    0x18
#define REG_IOP_RECV1    0x1C
#define REG_AP_CTRL      0x20
#define REG_AP_SEND0     0x30
#define REG_AP_SEND1     0x34
#define REG_AP_RECV0     0x38
#define REG_AP_RECV1     0x3C

static void apple_a7iop_v2_mailbox_reg_write(void* opaque, hwaddr addr, const uint64_t data, unsigned size)
{
    AppleA7IOPMailbox* s = opaque;

    switch (addr) {
        case REG_INT_MASK_SET: apple_a7iop_mailbox_set_int_mask(s, (uint32_t)data); break;
        case REG_INT_MASK_CLR: apple_a7iop_mailbox_clear_int_mask(s, (uint32_t)data); break;
        case REG_IOP_CTRL    : apple_a7iop_mailbox_set_iop_ctrl(s, (uint32_t)data); break;
        case REG_AP_CTRL     : apple_a7iop_mailbox_set_ap_ctrl(s, (uint32_t)data); break;
        case REG_IOP_SEND0   :
        case REG_IOP_SEND1:
            WITH_QEMU_LOCK_GUARD(&s->lock) { memcpy(s->iop_send_reg.data + (addr - REG_IOP_SEND0), &data, size); }
            if (addr + size - 4 == REG_IOP_SEND1) { apple_a7iop_mailbox_send_iop(s, &s->iop_send_reg); }
            break;
        case REG_AP_SEND0:
        case REG_AP_SEND1:
            WITH_QEMU_LOCK_GUARD(&s->lock) { memcpy(s->ap_send_reg.data + (addr - REG_AP_SEND0), &data, size); }
            if (addr + size - 4 == REG_AP_SEND1) { apple_a7iop_mailbox_send_ap(s, &s->ap_send_reg); }
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s unknown @ 0x" HWADDR_FMT_plx " value 0x%" PRIx64 "\n", __FUNCTION__, addr,
                          data);
            break;
    }
}

static uint64_t apple_a7iop_v2_mailbox_reg_read(void* opaque, hwaddr addr, unsigned size)
{
    AppleA7IOPMailbox* s   = opaque;
    uint64_t           ret = 0;

    switch (addr) {
        case REG_INT_MASK_SET:
        case REG_INT_MASK_CLR: return apple_a7iop_mailbox_get_int_mask(s);
        case REG_IOP_CTRL    : return apple_a7iop_mailbox_get_iop_ctrl(s);
        case REG_AP_CTRL     : return apple_a7iop_mailbox_get_ap_ctrl(s);
        case REG_IOP_RECV0:
            if (!apple_a7iop_mailbox_recv_iop(s, &s->iop_recv_reg)) {
                WITH_QEMU_LOCK_GUARD(&s->lock) { memset(&s->iop_recv_reg, 0, sizeof(s->iop_recv_reg)); }
            }
            QEMU_FALLTHROUGH;
        case REG_IOP_RECV1:
            WITH_QEMU_LOCK_GUARD(&s->lock) { memcpy(&ret, s->iop_recv_reg.data + (addr - REG_IOP_RECV0), size); }
            break;
        case REG_AP_RECV0:
            if (!apple_a7iop_mailbox_recv_ap(s, &s->ap_recv_reg)) {
                WITH_QEMU_LOCK_GUARD(&s->lock) { memset(&s->ap_recv_reg, 0, sizeof(s->ap_recv_reg)); }
            }
            QEMU_FALLTHROUGH;
        case REG_AP_RECV1:
            WITH_QEMU_LOCK_GUARD(&s->lock) { memcpy(&ret, s->ap_recv_reg.data + (addr - REG_AP_RECV0), size); }
            break;
        default: qemu_log_mask(LOG_UNIMP, "%s unknown @ 0x" HWADDR_FMT_plx "\n", __FUNCTION__, addr); break;
    }

    return ret;
}

static const MemoryRegionOps apple_a7iop_v2_mailbox_reg_ops = {
    .write                 = apple_a7iop_v2_mailbox_reg_write,
    .read                  = apple_a7iop_v2_mailbox_reg_read,
    .endianness            = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size  = 4,
    .impl.max_access_size  = 8,
    .valid.unaligned       = false,
};

void apple_a7iop_mailbox_init_mmio_v2(AppleA7IOPMailbox* s, const char* name)
{ memory_region_init_io(&s->mmio, OBJECT(s), &apple_a7iop_v2_mailbox_reg_ops, s, name, REG_AP_RECV1 + 4); }
