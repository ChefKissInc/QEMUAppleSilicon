/*
 * Apple A7IOP V2 Registers.
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
#include "exec/hwaddr.h"
#include "hw/misc/apple-silicon/a7iop/core.h"
#include "hw/misc/apple-silicon/a7iop/private.h"
#include "qemu/log.h"

#define REG_AXI_BASE_LO (0x8)
#define REG_AXI_BASE_HI (0x10)
#define REG_AXI_START_LO (0x18)
#define REG_AXI_START_HI (0x20)
#define REG_AXI_END_LO (0x28)
#define REG_AXI_END_HI (0x30)
#define REG_REMAP_0 (0x38)
#define REG_REMAP_1 (0x3C)
#define REG_CPU_CTRL (0x44)
#define REG_CPU_STATUS (0x48)
#define REG_KIC_GLB_CFG (0x80C)
#define KIC_GLB_CFG_TIMER_EN (1 << 1)
#define REG_KIC_MAILBOX_EXT_SET (0xC00)
#define REG_KIC_MAILBOX_EXT_CLR (0xC04)
#define REG_IDLE_STATUS (0x4040)
#define REG_KIC_TMR_CFG1 (0xC000)
#define KIC_TMR_CFG_FSL_TIMER (0 << 4)
#define KIC_TMR_CFG_FSL_SW (1 << 4)
#define KIC_TMR_CFG_FSL_EXTERNAL (2 << 4)
#define KIC_TMR_CFG_SMD_FIQ (0 << 3)
#define KIC_TMR_CFG_SMD_IRQ (1 << 3)
#define KIC_TMR_CFG_EMD_IRQ (1 << 2)
#define KIC_TMR_CFG_IMD_FIQ (0 << 1)
#define KIC_TMR_CFG_IMD_IRQ (1 << 1)
#define KIC_TMR_CFG_EN (1 << 0)
#define KIC_TMR_CFG_NMI                                               \
    (KIC_TMR_CFG_FSL_SW | KIC_TMR_CFG_SMD_FIQ | KIC_TMR_CFG_IMD_FIQ | \
     KIC_TMR_CFG_EN)
#define REG_KIC_TMR_CFG2 (0xC004)
#define REG_KIC_TMR_ISR (0xC018)
#define REG_KIC_TMR_STATE_SET1 (0xC020)
#define KIC_TMR_STATE_SET_SGT (1 << 0)
#define REG_KIC_TMR_STATE_SET2 (0xC024)
#define REG_KIC_GLB_TIME_BASE_LO (0xC030)
#define REG_KIC_GLB_TIME_BASE_HI (0xC038)

#define IOP_MAILBOX_REG_BASE (0xB80)

static void apple_a7iop_v2_reg_write(void *opaque, hwaddr addr,
                                     const uint64_t data, unsigned size)
{
    AppleA7IOP *s = opaque;

    switch (addr) {
    case REG_CPU_CTRL:
        apple_a7iop_set_cpu_ctrl(s, (uint32_t)data);
        break;
    case REG_CPU_STATUS:
        apple_a7iop_set_cpu_status(s, (uint32_t)data);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "A7IOP(%s): Unknown write to 0x" HWADDR_FMT_plx
                      " of value 0x" HWADDR_FMT_plx "\n",
                      s->role, addr, data);
        break;
    }
}

static uint64_t apple_a7iop_v2_reg_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    AppleA7IOP *s = opaque;

    switch (addr) {
    case REG_CPU_CTRL:
        return apple_a7iop_get_cpu_ctrl(s);
    case REG_CPU_STATUS:
        return apple_a7iop_get_cpu_status(s);
    default:
        qemu_log_mask(LOG_UNIMP,
                      "A7IOP(%s): Unknown read from 0x" HWADDR_FMT_plx "\n",
                      s->role, addr);
        return 0;
    }
}

static const MemoryRegionOps apple_a7iop_v2_reg_ops = {
    .write = apple_a7iop_v2_reg_write,
    .read = apple_a7iop_v2_reg_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size = 4,
    .impl.max_access_size = 8,
    .valid.unaligned = false,
};

void apple_a7iop_init_mmio_v2(AppleA7IOP *s, uint64_t mmio_size)
{
    SysBusDevice *sbd;
    char name[32];

    sbd = SYS_BUS_DEVICE(s);

    snprintf(name, sizeof(name), TYPE_APPLE_A7IOP ".%s.regs", s->role);
    memory_region_init_io(&s->mmio, OBJECT(s), &apple_a7iop_v2_reg_ops, s, name,
                          mmio_size);
    sysbus_init_mmio(sbd, &s->mmio);

    memory_region_add_subregion_overlap(&s->mmio, IOP_MAILBOX_REG_BASE,
                                        &s->iop_mailbox->mmio, 1);
    memory_region_add_subregion_overlap(&s->mmio, AKF_STRIDE,
                                        &s->ap_mailbox->mmio, 1);
}
