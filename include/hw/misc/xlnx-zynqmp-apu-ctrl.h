/*
 * QEMU model of ZynqMP APU Control.
 *
 * Copyright (c) 2013-2022 Xilinx Inc
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com> and
 * Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 */
#ifndef HW_MISC_XLNX_ZYNQMP_APU_CTRL_H
#define HW_MISC_XLNX_ZYNQMP_APU_CTRL_H

#include "hw/sysbus.h"
#include "hw/register.h"
#include "target/arm/cpu-qom.h"

#define TYPE_XLNX_ZYNQMP_APU_CTRL "xlnx.apu-ctrl"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxZynqMPAPUCtrl, XLNX_ZYNQMP_APU_CTRL)

REG32(APU_ERR_CTRL, 0x0)
    REG_FIELD(APU_ERR_CTRL, PSLVERR, 0, 1)
REG32(ISR, 0x10)
    REG_FIELD(ISR, INV_APB, 0, 1)
REG32(IMR, 0x14)
    REG_FIELD(IMR, INV_APB, 0, 1)
REG32(IEN, 0x18)
    REG_FIELD(IEN, INV_APB, 0, 1)
REG32(IDS, 0x1c)
    REG_FIELD(IDS, INV_APB, 0, 1)
REG32(CONFIG_0, 0x20)
    REG_FIELD(CONFIG_0, CFGTE, 24, 4)
    REG_FIELD(CONFIG_0, CFGEND, 16, 4)
    REG_FIELD(CONFIG_0, VINITHI, 8, 4)
    REG_FIELD(CONFIG_0, AA64NAA32, 0, 4)
REG32(CONFIG_1, 0x24)
    REG_FIELD(CONFIG_1, L2RSTDISABLE, 29, 1)
    REG_FIELD(CONFIG_1, L1RSTDISABLE, 28, 1)
    REG_FIELD(CONFIG_1, CP15DISABLE, 0, 4)
REG32(RVBARADDR0L, 0x40)
    REG_FIELD(RVBARADDR0L, ADDR, 2, 30)
REG32(RVBARADDR0H, 0x44)
    REG_FIELD(RVBARADDR0H, ADDR, 0, 8)
REG32(RVBARADDR1L, 0x48)
    REG_FIELD(RVBARADDR1L, ADDR, 2, 30)
REG32(RVBARADDR1H, 0x4c)
    REG_FIELD(RVBARADDR1H, ADDR, 0, 8)
REG32(RVBARADDR2L, 0x50)
    REG_FIELD(RVBARADDR2L, ADDR, 2, 30)
REG32(RVBARADDR2H, 0x54)
    REG_FIELD(RVBARADDR2H, ADDR, 0, 8)
REG32(RVBARADDR3L, 0x58)
    REG_FIELD(RVBARADDR3L, ADDR, 2, 30)
REG32(RVBARADDR3H, 0x5c)
    REG_FIELD(RVBARADDR3H, ADDR, 0, 8)
REG32(ACE_CTRL, 0x60)
    REG_FIELD(ACE_CTRL, AWQOS, 16, 4)
    REG_FIELD(ACE_CTRL, ARQOS, 0, 4)
REG32(SNOOP_CTRL, 0x80)
    REG_FIELD(SNOOP_CTRL, ACE_INACT, 4, 1)
    REG_FIELD(SNOOP_CTRL, ACP_INACT, 0, 1)
REG32(PWRCTL, 0x90)
    REG_FIELD(PWRCTL, CLREXMONREQ, 17, 1)
    REG_FIELD(PWRCTL, L2FLUSHREQ, 16, 1)
    REG_FIELD(PWRCTL, CPUPWRDWNREQ, 0, 4)
REG32(PWRSTAT, 0x94)
    REG_FIELD(PWRSTAT, CLREXMONACK, 17, 1)
    REG_FIELD(PWRSTAT, L2FLUSHDONE, 16, 1)
    REG_FIELD(PWRSTAT, DBGNOPWRDWN, 0, 4)

#define APU_R_MAX ((R_PWRSTAT) + 1)

#define APU_MAX_CPU    4

struct XlnxZynqMPAPUCtrl {
    SysBusDevice busdev;

    ARMCPU *cpus[APU_MAX_CPU];
    /* WFIs towards PMU. */
    qemu_irq wfi_out[4];
    /* CPU Power status towards INTC Redirect. */
    qemu_irq cpu_power_status[4];
    qemu_irq irq_imr;

    uint8_t cpu_pwrdwn_req;
    uint8_t cpu_in_wfi;

    RegisterInfoArray *reg_array;
    uint32_t regs[APU_R_MAX];
    RegisterInfo regs_info[APU_R_MAX];
};

#endif
