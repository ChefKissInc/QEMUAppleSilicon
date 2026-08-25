/*
 * Apple SEP Boot Monitor.
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
#include "exec/cputlb.h"
#include "exec/tb-flush.h"
#include "hw/arm/a13.h"
#include "hw/arm/sep/private.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/tcg.h"

#define BOOT_MONITOR_REG_SIZE (0x4000)    // ?

struct AppleSEPBootMonitorState
{
    SysBusDevice parent_obj;

    AppleSEPState* sep;
    MemoryRegion   boot_monitor_mr;
    uint8_t        boot_monitor_regs[BOOT_MONITOR_REG_SIZE];
};

static void boot_monitor_reg_write(void* opaque, hwaddr addr, uint64_t data, unsigned size)
{
    AppleSEPBootMonitorState* s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->sep->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
        case 0x04:              // some status flag, bit0
            data &= ~BIT(0);    // reset bit0 for read
            QEMU_FALLTHROUGH;
        case 0x08:    // maybe some command0?
                      // 0x2: something about PKA
                      // 0x3: ?
                      // 0x4: during resume
                      // 0x10: during first/cold boot
                      // 0x11: ?
                      // 0x12: ?
        case 0x10:    // maybe some command1?
        case 0x14:    // ?
        case 0x20:    // load address low
        case 0x24:    // load address high
                      // 0x0000000340000000
        case 0x28:    // end address low
        case 0x2C:    // end address high
                      // 0x0000000340010000, middle of kernel __text
        case 0x30:    // unknown1 address low
        case 0x34:    // unknown1 address high
                      // 0x000000034004c000 == base of SEPD
        case 0x38:    // unknown2 address low
        case 0x3C:    // unknown2 address high
                      // 0x00000003403d0000 == 0xc000 of SEPOS, middle of __text
                      // 0x0000000340464000 == 0x1c000 of SEPOS, middle of __cstring
        case 0x40:    // unknown0 address low
        case 0x44:    // unknown0 address high
            // 0x0000000000000000
            goto jump_default;
        case 0x48:      // randomness low
        case 0x4C:      // randomness high
        case 0x50: {    // randomness lock
            bool randomness_locked = (((uint32_t*)s->boot_monitor_regs)[0x50 / 4] & BIT(0)) != 0;
            if (randomness_locked) {
                DPRINTF("SEP Boot Monitor: Locked write at 0x" HWADDR_FMT_plx " with value 0x%" PRIX64 "\n", addr,
                        data);
                break;
            }
            QEMU_FALLTHROUGH;
        }
        default:
        jump_default:
            DPRINTF("SEP Boot Monitor: Unknown write at 0x" HWADDR_FMT_plx " with value 0x%" PRIX64 "\n", addr, data);
            memcpy(&s->boot_monitor_regs[addr], &data, size);
            break;
    }
}

static uint64_t boot_monitor_reg_read(void* opaque, hwaddr addr, unsigned size)
{
    AppleSEPBootMonitorState* s   = opaque;
    uint64_t                  ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->sep->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
        case 0x04:    // some status flag, bit0, maybe "is active"
            goto jump_default;
        case 0x0C:    // must return 0x0
            // other possible values: 0x1/0x2/0x3, maybe even 0x4
            // maybe error codes?
            ret = 0x0;
            return ret;
        default:
            DPRINTF("SEP Boot Monitor: Unknown read at 0x" HWADDR_FMT_plx "\n", addr);
        jump_default:
            memcpy(&ret, &s->boot_monitor_regs[addr], size);
            break;
    }

    return ret;
}

static const MemoryRegionOps boot_monitor_reg_ops = {
    .write                 = boot_monitor_reg_write,
    .read                  = boot_monitor_reg_read,
    .endianness            = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size  = 4,
    .impl.max_access_size  = 4,
    .valid.unaligned       = false,
};

static void apple_sep_cpu_moni_reset_regs(CPUState* cpu, hwaddr load_addr, hwaddr pwr_dn_save)
{
    ARMCPU*        arm_cpu                   = container_of(cpu, ARMCPU, parent_obj);
    AppleA13State* acpu                      = container_of(arm_cpu, AppleA13State, parent_obj);
    CPUARMState*   env                       = &arm_cpu->env;
    acpu->A13_CPREG_VAR_NAME(ARM64_REG_HID5) = 0x0;
    acpu->A13_CPREG_VAR_NAME(S3_4_c15_c0_5)  = 0x0;
    // clearing ttbr0_el1 is absolutely required for sepfw 26
    // not clearing everything will lead to some tcg/tlb sigsegv
    // not sure whether to only clear _el[1] or the entries of every el
    // but let's clear all. at least "ttbr1_el[1]" seems also to be required.
    memset(env->regs, 0, sizeof(env->regs));
    memset(env->xregs, 0, sizeof(env->xregs));
    memset(env->cp15.ttbr0_el, 0, sizeof(env->cp15.ttbr0_el));
    memset(env->cp15.ttbr1_el, 0, sizeof(env->cp15.ttbr1_el));
    memset(env->cp15.mair_el, 0, sizeof(env->cp15.mair_el));
    memset(env->elr_el, 0, sizeof(env->elr_el));
    // should sp_el be clear/zero before and after?
    memset(env->sp_el, 0, sizeof(env->sp_el));
    memset(env->cp15.esr_el, 0, sizeof(env->cp15.esr_el));
    memset(env->banked_spsr, 0, sizeof(env->banked_spsr));
    memset(env->cp15.tcr_el, 0, sizeof(env->cp15.tcr_el));
    memset(env->cp15.sctlr_el, 0, sizeof(env->cp15.sctlr_el));
    memset(&env->keys, 0, sizeof(env->keys));

    acpu->A13_CPREG_VAR_NAME(SYS_ACC_PWR_DN_SAVE) = pwr_dn_save;
    env->cp15.rvbar                               = load_addr;
    env->cp15.vbar_el[1]                          = load_addr;
    cpu_set_pc(cpu, load_addr);
}

static hwaddr apple_sep_boot_monitor_load_addr(AppleSEPBootMonitorState* s)
{ return ((hwaddr*)s->boot_monitor_regs)[0x20 / 8]; }

// some race conditions might happen before, during and/or after the jump.
static void apple_sep_cpu_moni_jump(CPUState* cpu, run_on_cpu_data data)
{
    ARMCPU*                   arm_cpu = container_of(cpu, ARMCPU, parent_obj);
    AppleSEPBootMonitorState* s       = data.host_ptr;

    hwaddr load_addr = apple_sep_boot_monitor_load_addr(s);

    DPRINTF("%s: have load_addr 0x" HWADDR_FMT_plx "\n", __func__, load_addr);

    if (load_addr == 0) { return; }

    // some specific, non currently used(?), cpu_ functions will require bql
    // BQL_LOCK_GUARD();

    DPRINTF("%s: before cpu_set_pc: base=0x%" VADDR_PRIX "\n", __func__, load_addr);

    AppleA13State* acpu        = container_of(arm_cpu, AppleA13State, parent_obj);
    hwaddr         pwr_dn_save = acpu->A13_CPREG_VAR_NAME(SYS_ACC_PWR_DN_SAVE);
    cpu_pause(cpu);
    apple_sep_cpu_moni_reset_regs(cpu, load_addr, pwr_dn_save);

    // possible workaround for intermittent sep boot errors
    // does it matter whether a tlb_flush happens before or after a write?
    if (tcg_enabled()) {
        arm_rebuild_hflags(&arm_cpu->env);
        tb_flush__exclusive_or_serial();
        tlb_flush(cpu);
    }
    cpu_resume(cpu);
    // using qemu_irq_raise ARM_CPU_IRQ here will cause a7iop atomic sigsegv
}

#ifdef SEP_DISABLE_ASLR
static void disable_aslr_SYS_ACC_PWR_DN_SAVE(AppleSEPState* s)
{
    DPRINTF("SEP_BOOT_MONITOR_JUMP: Disable ASLR SYS_ACC_PWR_DN_SAVE\n");
    AppleA13State* acpu        = APPLE_A13(s->cpu);
    hwaddr         pwr_dn_save = acpu->A13_CPREG_VAR_NAME(SYS_ACC_PWR_DN_SAVE);
    AddressSpace*  nsas        = &address_space_memory;
    address_space_set(nsas, pwr_dn_save + 0x80, 0, 0x40, MEMTXATTRS_UNSPECIFIED);
}
#endif

void apple_sep_boot_monitor_jump(AppleSEPBootMonitorState* s)
{
    AppleSEPState* sep = s->sep;

    if (sep->modern) {
#ifdef SEP_DISABLE_ASLR
        if (apple_sep_boot_monitor_load_addr(s) != 0) { disable_aslr_SYS_ACC_PWR_DN_SAVE(sep); }
#endif
        async_safe_run_on_cpu(CPU(sep->cpu), apple_sep_cpu_moni_jump, RUN_ON_CPU_HOST_PTR(s));
    }
}

static void apple_sep_boot_monitor_reset_enter(Object* obj, ResetType type)
{
    AppleSEPBootMonitorState* s = APPLE_SEP_BOOT_MONITOR(obj);

    memset(s->boot_monitor_regs, 0, sizeof(s->boot_monitor_regs));
}

static void apple_sep_boot_monitor_realize(DeviceState* dev, Error** errp)
{
    AppleSEPBootMonitorState* s   = APPLE_SEP_BOOT_MONITOR(dev);
    SysBusDevice*             sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->boot_monitor_mr, OBJECT(s), &boot_monitor_reg_ops, s, "regs", BOOT_MONITOR_REG_SIZE);
    sysbus_init_mmio(sbd, &s->boot_monitor_mr);
}

static void apple_sep_boot_monitor_class_init(ObjectClass* klass, const void* class_data)
{
    ResettableClass* rc = RESETTABLE_CLASS(klass);
    DeviceClass*     dc = DEVICE_CLASS(klass);

    rc->phases.enter = apple_sep_boot_monitor_reset_enter;
    dc->realize      = apple_sep_boot_monitor_realize;
}

static const TypeInfo apple_sep_boot_monitor_type_info = {
    .name           = TYPE_APPLE_SEP_BOOT_MONITOR,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .class_init     = apple_sep_boot_monitor_class_init,
    .instance_size  = sizeof(AppleSEPBootMonitorState),
    .instance_align = __alignof__(AppleSEPBootMonitorState),
};

static void apple_sep_boot_monitor_register_types(void) { type_register_static(&apple_sep_boot_monitor_type_info); }

type_init(apple_sep_boot_monitor_register_types);

AppleSEPBootMonitorState* apple_sep_boot_monitor_create(AppleSEPState* sep)
{
    AppleSEPBootMonitorState* s = APPLE_SEP_BOOT_MONITOR(qdev_new(TYPE_APPLE_SEP_BOOT_MONITOR));
    s->sep                      = sep;
    return s;
}
