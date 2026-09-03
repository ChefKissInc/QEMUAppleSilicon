/*
 * QEMU ARM TCG-only CPUs.
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "internals.h"
#include "hw/boards.h"
#include "cpregs.h"

/* Share AArch32 -cpu max features with AArch64. */
void aa32_max_features(ARMCPU* cpu)
{
    uint32_t         t;
    ARMISARegisters* isar = &cpu->isar;

    /* Add additional features supported by QEMU */
    t = GET_IDREG(isar, ID_ISAR5);
    t = REG_FIELD_DP32(t, ID_ISAR5, AES, 2);  /* FEAT_PMULL */
    t = REG_FIELD_DP32(t, ID_ISAR5, SHA1, 1); /* FEAT_SHA1 */
    t = REG_FIELD_DP32(t, ID_ISAR5, SHA2, 1); /* FEAT_SHA256 */
    t = REG_FIELD_DP32(t, ID_ISAR5, CRC32, 1);
    t = REG_FIELD_DP32(t, ID_ISAR5, RDM, 1);  /* FEAT_RDM */
    t = REG_FIELD_DP32(t, ID_ISAR5, VCMA, 1); /* FEAT_FCMA */
    SET_IDREG(isar, ID_ISAR5, t);

    t = GET_IDREG(isar, ID_ISAR6);
    t = REG_FIELD_DP32(t, ID_ISAR6, JSCVT, 1);   /* FEAT_JSCVT */
    t = REG_FIELD_DP32(t, ID_ISAR6, DP, 1);      /* Feat_DotProd */
    t = REG_FIELD_DP32(t, ID_ISAR6, FHM, 1);     /* FEAT_FHM */
    t = REG_FIELD_DP32(t, ID_ISAR6, SB, 1);      /* FEAT_SB */
    t = REG_FIELD_DP32(t, ID_ISAR6, SPECRES, 1); /* FEAT_SPECRES */
    t = REG_FIELD_DP32(t, ID_ISAR6, BF16, 1);    /* FEAT_AA32BF16 */
    t = REG_FIELD_DP32(t, ID_ISAR6, I8MM, 1);    /* FEAT_AA32I8MM */
    SET_IDREG(isar, ID_ISAR6, t);

    t               = cpu->isar.mvfr1;
    t               = REG_FIELD_DP32(t, MVFR1, FPHP, 3);   /* FEAT_FP16 */
    t               = REG_FIELD_DP32(t, MVFR1, SIMDHP, 2); /* FEAT_FP16 */
    cpu->isar.mvfr1 = t;

    t               = cpu->isar.mvfr2;
    t               = REG_FIELD_DP32(t, MVFR2, SIMDMISC, 3); /* SIMD MaxNum */
    t               = REG_FIELD_DP32(t, MVFR2, FPMISC, 4);   /* FP MaxNum */
    cpu->isar.mvfr2 = t;

    FIELD_DP32_IDREG(isar, ID_MMFR3, PAN, 2); /* FEAT_PAN2 */

    t = GET_IDREG(isar, ID_MMFR4);
    t = REG_FIELD_DP32(t, ID_MMFR4, HPDS, 2); /* FEAT_HPDS2 */
    t = REG_FIELD_DP32(t, ID_MMFR4, AC2, 1);  /* ACTLR2, HACTLR2 */
    t = REG_FIELD_DP32(t, ID_MMFR4, CNP, 1);  /* FEAT_TTCNP */
    t = REG_FIELD_DP32(t, ID_MMFR4, XNX, 1);  /* FEAT_XNX */
    t = REG_FIELD_DP32(t, ID_MMFR4, EVT, 2);  /* FEAT_EVT */
    SET_IDREG(isar, ID_MMFR4, t);

    FIELD_DP32_IDREG(isar, ID_MMFR5, ETS, 2); /* FEAT_ETS2 */

    t = GET_IDREG(isar, ID_PFR0);
    t = REG_FIELD_DP32(t, ID_PFR0, CSV2, 2); /* FEAT_CSV2 */
    t = REG_FIELD_DP32(t, ID_PFR0, DIT, 1);  /* FEAT_DIT */
    t = REG_FIELD_DP32(t, ID_PFR0, RAS, 1);  /* FEAT_RAS */
    SET_IDREG(isar, ID_PFR0, t);

    t = GET_IDREG(isar, ID_PFR2);
    t = REG_FIELD_DP32(t, ID_PFR2, CSV3, 1); /* FEAT_CSV3 */
    t = REG_FIELD_DP32(t, ID_PFR2, SSBS, 1); /* FEAT_SSBS */
    SET_IDREG(isar, ID_PFR2, t);

    t = GET_IDREG(isar, ID_DFR0);
    t = REG_FIELD_DP32(t, ID_DFR0, COPDBG, 10);  /* FEAT_Debugv8p8 */
    t = REG_FIELD_DP32(t, ID_DFR0, COPSDBG, 10); /* FEAT_Debugv8p8 */
    t = REG_FIELD_DP32(t, ID_DFR0, PERFMON, 6);  /* FEAT_PMUv3p5 */
    SET_IDREG(isar, ID_DFR0, t);

    /* Debug ID registers. */

    /* Bit[15] is RES1, Bit[13] and Bits[11:0] are RES0. */
    t                 = 0x00008000;
    t                 = REG_FIELD_DP32(t, DBGDIDR, SE_IMP, 1);
    t                 = REG_FIELD_DP32(t, DBGDIDR, NSUHD_IMP, 1);
    t                 = REG_FIELD_DP32(t, DBGDIDR, VERSION, 10); /* FEAT_Debugv8p8 */
    t                 = REG_FIELD_DP32(t, DBGDIDR, CTX_CMPS, 1);
    t                 = REG_FIELD_DP32(t, DBGDIDR, BRPS, 5);
    t                 = REG_FIELD_DP32(t, DBGDIDR, WRPS, 3);
    cpu->isar.dbgdidr = t;

    t                  = 0;
    t                  = REG_FIELD_DP32(t, DBGDEVID, PCSAMPLE, 3);
    t                  = REG_FIELD_DP32(t, DBGDEVID, WPADDRMASK, 1);
    t                  = REG_FIELD_DP32(t, DBGDEVID, BPADDRMASK, 15);
    t                  = REG_FIELD_DP32(t, DBGDEVID, VECTORCATCH, 0);
    t                  = REG_FIELD_DP32(t, DBGDEVID, VIRTEXTNS, 1);
    t                  = REG_FIELD_DP32(t, DBGDEVID, DOUBLELOCK, 1);
    t                  = REG_FIELD_DP32(t, DBGDEVID, AUXREGS, 0);
    t                  = REG_FIELD_DP32(t, DBGDEVID, CIDMASK, 0);
    cpu->isar.dbgdevid = t;

    /* Bits[31:4] are RES0. */
    t                   = 0;
    t                   = REG_FIELD_DP32(t, DBGDEVID1, PCSROFFSET, 2);
    cpu->isar.dbgdevid1 = t;

    FIELD_DP32_IDREG(isar, ID_DFR1, HPMN0, 1); /* FEAT_HPMN0 */
}

#ifndef TARGET_AARCH64
/*
 * -cpu max: a CPU with as many features enabled as our emulation supports.
 * The version of '-cpu max' for qemu-system-aarch64 is defined in cpu64.c;
 * this only needs to handle 32 bits, and need not care about KVM.
 */
static void arm_max_initfn(Object* obj)
{
    ARMCPU*          cpu  = ARM_CPU(obj);
    ARMISARegisters* isar = &cpu->isar;

    /* aarch64_a57_initfn, advertising none of the aarch64 features */
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr        = 0x411fd070;
    cpu->revidr      = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->isar.mvfr0  = 0x10110222;
    cpu->isar.mvfr1  = 0x12111111;
    cpu->isar.mvfr2  = 0x00000043;
    cpu->ctr         = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    SET_IDREG(isar, ID_PFR0, 0x00000131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_DFR0, 0x03010066);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_MMFR0, 0x10101105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02102211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00011142);
    SET_IDREG(isar, ID_ISAR5, 0x00011121);
    SET_IDREG(isar, ID_ISAR6, 0);
    cpu->isar.reset_pmcr_el0 = 0x41013000;
    SET_IDREG(isar, CLIDR, 0x0a200023);
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x70ffe07a; /* 2048KB L2 cache */
    define_cortex_a72_a57_a53_cp_reginfo(cpu);

    aa32_max_features(cpu);
}
#endif /* !TARGET_AARCH64 */

static const ARMCPUInfo arm_tcg_cpus[] = {
#ifndef TARGET_AARCH64
    {.name = "max", .initfn = arm_max_initfn},
#endif
};

static void arm_tcg_cpu_register_types(void)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(arm_tcg_cpus); ++i) { arm_cpu_register(&arm_tcg_cpus[i]); }
}

type_init(arm_tcg_cpu_register_types)
