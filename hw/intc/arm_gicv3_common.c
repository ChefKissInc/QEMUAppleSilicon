/*
 * ARM GICv3 support - common bits of emulated and KVM kernel model
 *
 * Copyright (c) 2012 Linaro Limited
 * Copyright (c) 2015 Huawei.
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Peter Maydell
 * Reworked for GICv3 by Shlomo Pongratz and Pavel Fedin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/core/cpu.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/qdev-properties.h"
#include "gicv3_internal.h"
#include "system/kvm.h"


void gicv3_init_irqs_and_mmio(GICv3State *s, qemu_irq_handler handler,
                              const MemoryRegionOps *ops)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    int i;
    int cpuidx;

    /* For the GIC, also expose incoming GPIO lines for PPIs for each CPU.
     * GPIO array layout is thus:
     *  [0..N-1] spi
     *  [N..N+31] PPIs for CPU 0
     *  [N+32..N+63] PPIs for CPU 1
     *   ...
     */
    i = s->num_irq - GIC_INTERNAL + GIC_INTERNAL * s->num_cpu;
    qdev_init_gpio_in(DEVICE(s), handler, i);

    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->cpu[i].parent_irq);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->cpu[i].parent_fiq);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->cpu[i].parent_virq);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->cpu[i].parent_vfiq);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->cpu[i].parent_nmi);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->cpu[i].parent_vnmi);
    }

    memory_region_init_io(&s->iomem_dist, OBJECT(s), ops, s,
                          "gicv3_dist", 0x10000);
    sysbus_init_mmio(sbd, &s->iomem_dist);

    s->redist_regions = g_new0(GICv3RedistRegion, s->nb_redist_regions);
    cpuidx = 0;
    for (i = 0; i < s->nb_redist_regions; i++) {
        char *name = g_strdup_printf("gicv3_redist_region[%d]", i);
        GICv3RedistRegion *region = &s->redist_regions[i];

        region->gic = s;
        region->cpuidx = cpuidx;
        cpuidx += s->redist_region_count[i];

        memory_region_init_io(&region->iomem, OBJECT(s),
                              ops ? &ops[1] : NULL, region, name,
                              s->redist_region_count[i] * gicv3_redist_size(s));
        sysbus_init_mmio(sbd, &region->iomem);
        g_free(name);
    }
}

static void arm_gicv3_common_realize(DeviceState *dev, Error **errp)
{
    GICv3State *s = ARM_GICV3_COMMON(dev);
    int i, rdist_capacity, cpuidx;

    /*
     * This GIC device supports only revisions 3 and 4. The GICv1/v2
     * is a separate device.
     * Note that subclasses of this device may impose further restrictions
     * on the GIC revision: notably, the in-kernel KVM GIC doesn't
     * support GICv4.
     */
    if (s->revision != 3 && s->revision != 4) {
        error_setg(errp, "unsupported GIC revision %d", s->revision);
        return;
    }

    if (s->num_irq > GICV3_MAXIRQ) {
        error_setg(errp,
                   "requested %u interrupt lines exceeds GIC maximum %d",
                   s->num_irq, GICV3_MAXIRQ);
        return;
    }
    if (s->num_irq < GIC_INTERNAL) {
        error_setg(errp,
                   "requested %u interrupt lines is below GIC minimum %d",
                   s->num_irq, GIC_INTERNAL);
        return;
    }
    if (s->num_cpu == 0) {
        error_setg(errp, "num-cpu must be at least 1");
        return;
    }

    /* ITLinesNumber is represented as (N / 32) - 1, so this is an
     * implementation imposed restriction, not an architectural one,
     * so we don't have to deal with bitfields where only some of the
     * bits in a 32-bit word should be valid.
     */
    if (s->num_irq % 32) {
        error_setg(errp,
                   "%d interrupt lines unsupported: not divisible by 32",
                   s->num_irq);
        return;
    }

    if (s->lpi_enable && !s->dma) {
        error_setg(errp, "Redist-ITS: Guest 'sysmem' reference link not set");
        return;
    }

    rdist_capacity = 0;
    for (i = 0; i < s->nb_redist_regions; i++) {
        rdist_capacity += s->redist_region_count[i];
    }
    if (rdist_capacity != s->num_cpu) {
        error_setg(errp, "Capacity of the redist regions(%d) "
                   "does not match the number of vcpus(%d)",
                   rdist_capacity, s->num_cpu);
        return;
    }

    if (s->lpi_enable) {
        address_space_init(&s->dma_as, s->dma,
                           "gicv3-its-sysmem");
    }

    s->cpu = g_new0(GICv3CPUState, s->num_cpu);

    for (i = 0; i < s->num_cpu; i++) {
        CPUState *cpu = qemu_get_cpu(i);
        uint64_t cpu_affid;

        s->cpu[i].cpu = cpu;
        s->cpu[i].gic = s;
        /* Store GICv3CPUState in CPUARMState gicv3state pointer */
        gicv3_set_gicv3state(cpu, &s->cpu[i]);

        /* Pre-construct the GICR_TYPER:
         * For our implementation:
         *  Top 32 bits are the affinity value of the associated CPU
         *  CommonLPIAff == 01 (redistributors with same Aff3 share LPI table)
         *  Processor_Number == CPU index starting from 0
         *  DPGS == 0 (GICR_CTLR.DPG* not supported)
         *  Last == 1 if this is the last redistributor in a series of
         *            contiguous redistributor pages
         *  DirectLPI == 0 (direct injection of LPIs not supported)
         *  VLPIS == 1 if vLPIs supported (GICv4 and up)
         *  PLPIS == 1 if LPIs supported
         */
        cpu_affid = object_property_get_uint(OBJECT(cpu), "mp-affinity", NULL);

        /* The CPU mp-affinity property is in MPIDR register format; squash
         * the affinity bytes into 32 bits as the GICR_TYPER has them.
         */
        cpu_affid = ((cpu_affid & 0xFF00000000ULL) >> 8) |
                     (cpu_affid & 0xFFFFFF);
        s->cpu[i].gicr_typer = (cpu_affid << 32) |
            (1 << 24) |
            (i << 8);

        if (s->lpi_enable) {
            s->cpu[i].gicr_typer |= GICR_TYPER_PLPIS;
            if (s->revision > 3) {
                s->cpu[i].gicr_typer |= GICR_TYPER_VLPIS;
            }
        }
    }

    /*
     * Now go through and set GICR_TYPER.Last for the final
     * redistributor in each region.
     */
    cpuidx = 0;
    for (i = 0; i < s->nb_redist_regions; i++) {
        cpuidx += s->redist_region_count[i];
        s->cpu[cpuidx - 1].gicr_typer |= GICR_TYPER_LAST;
    }

    s->itslist = g_ptr_array_new();
}

static void arm_gicv3_finalize(Object *obj)
{
    GICv3State *s = ARM_GICV3_COMMON(obj);

    g_free(s->redist_region_count);
}

static void arm_gicv3_common_reset_hold(Object *obj, ResetType type)
{
    GICv3State *s = ARM_GICV3_COMMON(obj);
    int i;

    for (i = 0; i < s->num_cpu; i++) {
        GICv3CPUState *cs = &s->cpu[i];

        cs->level = 0;
        cs->gicr_ctlr = 0;
        if (s->lpi_enable) {
            /* Our implementation supports clearing GICR_CTLR.EnableLPIs */
            cs->gicr_ctlr |= GICR_CTLR_CES;
        }
        cs->gicr_statusr[GICV3_S] = 0;
        cs->gicr_statusr[GICV3_NS] = 0;
        cs->gicr_waker = GICR_WAKER_ProcessorSleep | GICR_WAKER_ChildrenAsleep;
        cs->gicr_propbaser = 0;
        cs->gicr_pendbaser = 0;
        cs->gicr_vpropbaser = 0;
        cs->gicr_vpendbaser = 0;
        /* If we're resetting a TZ-aware GIC as if secure firmware
         * had set it up ready to start a kernel in non-secure, we
         * need to set interrupts to group 1 so the kernel can use them.
         * Otherwise they reset to group 0 like the hardware.
         */
        if (s->irq_reset_nonsecure) {
            cs->gicr_igroupr0 = 0xffffffff;
        } else {
            cs->gicr_igroupr0 = 0;
        }

        cs->gicr_ienabler0 = 0;
        cs->gicr_ipendr0 = 0;
        cs->gicr_iactiver0 = 0;
        cs->edge_trigger = 0xffff;
        cs->gicr_igrpmodr0 = 0;
        cs->gicr_nsacr = 0;
        memset(cs->gicr_ipriorityr, 0, sizeof(cs->gicr_ipriorityr));

        cs->hppi.prio = 0xff;
        cs->hppi.nmi = false;
        cs->hpplpi.prio = 0xff;
        cs->hpplpi.nmi = false;
        cs->hppvlpi.prio = 0xff;
        cs->hppvlpi.nmi = false;

        /* State in the CPU interface must *not* be reset here, because it
         * is part of the CPU's reset domain, not the GIC device's.
         */
    }

    /* For our implementation affinity routing is always enabled */
    if (s->security_extn) {
        s->gicd_ctlr = GICD_CTLR_ARE_S | GICD_CTLR_ARE_NS;
    } else {
        s->gicd_ctlr = GICD_CTLR_DS | GICD_CTLR_ARE;
    }

    s->gicd_statusr[GICV3_S] = 0;
    s->gicd_statusr[GICV3_NS] = 0;

    memset(s->group, 0, sizeof(s->group));
    memset(s->grpmod, 0, sizeof(s->grpmod));
    memset(s->enabled, 0, sizeof(s->enabled));
    memset(s->pending, 0, sizeof(s->pending));
    memset(s->active, 0, sizeof(s->active));
    memset(s->level, 0, sizeof(s->level));
    memset(s->edge_trigger, 0, sizeof(s->edge_trigger));
    memset(s->gicd_ipriority, 0, sizeof(s->gicd_ipriority));
    memset(s->gicd_irouter, 0, sizeof(s->gicd_irouter));
    memset(s->gicd_nsacr, 0, sizeof(s->gicd_nsacr));
    /* GICD_IROUTER are UNKNOWN at reset so in theory the guest must
     * write these to get sane behaviour and we need not populate the
     * pointer cache here; however having the cache be different for
     * "happened to be 0 from reset" and "guest wrote 0" would be
     * too confusing.
     */
    gicv3_cache_all_target_cpustates(s);

    if (s->irq_reset_nonsecure) {
        /* If we're resetting a TZ-aware GIC as if secure firmware
         * had set it up ready to start a kernel in non-secure, we
         * need to set interrupts to group 1 so the kernel can use them.
         * Otherwise they reset to group 0 like the hardware.
         */
        for (i = GIC_INTERNAL; i < s->num_irq; i++) {
            gicv3_gicd_group_set(s, i);
        }
    }
}

static const Property arm_gicv3_common_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", GICv3State, num_cpu, 1),
    DEFINE_PROP_UINT32("num-irq", GICv3State, num_irq, 32),
    DEFINE_PROP_UINT32("revision", GICv3State, revision, 3),
    DEFINE_PROP_BOOL("has-lpi", GICv3State, lpi_enable, 0),
    DEFINE_PROP_BOOL("has-nmi", GICv3State, nmi_support, 0),
    DEFINE_PROP_BOOL("has-security-extensions", GICv3State, security_extn, 0),
    DEFINE_PROP_UINT32("maintenance-interrupt-id", GICv3State, maint_irq, 0),
    /*
     * Compatibility property: force 8 bits of physical priority, even
     * if the CPU being emulated should have fewer.
     */
    DEFINE_PROP_BOOL("force-8-bit-prio", GICv3State, force_8bit_prio, 0),
    DEFINE_PROP_ARRAY("redist-region-count", GICv3State, nb_redist_regions,
                      redist_region_count, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_LINK("sysmem", GICv3State, dma, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void arm_gicv3_common_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = arm_gicv3_common_reset_hold;
    dc->realize = arm_gicv3_common_realize;
    device_class_set_props(dc, arm_gicv3_common_properties);
}

static const TypeInfo arm_gicv3_common_type = {
    .name = TYPE_ARM_GICV3_COMMON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GICv3State),
    .class_size = sizeof(ARMGICv3CommonClass),
    .class_init = arm_gicv3_common_class_init,
    .instance_finalize = arm_gicv3_finalize,
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&arm_gicv3_common_type);
}

type_init(register_types)

const char *gicv3_class_name(void)
{
    if (kvm_irqchip_in_kernel()) {
        return "kvm-arm-gicv3";
    } else {
        if (kvm_enabled()) {
            error_report("Userspace GICv3 is not supported with KVM");
            exit(1);
        }
        return "arm-gicv3";
    }
}
