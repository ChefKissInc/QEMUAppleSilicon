/*
 * QEMU AArch64 TCG CPUs
 *
 * Copyright (c) 2013 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "internals.h"
#include "cpu-features.h"
#include "cpregs.h"

static void cpu_max_get_sve_max_vq(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp)
{
    ARMCPU*  cpu = ARM_CPU(obj);
    uint32_t value;

    /* All vector lengths are disabled when SVE is off. */
    if (!cpu_isar_feature(aa64_sve, cpu)) { value = 0; }
    else {
        value = cpu->sve_max_vq;
    }
    visit_type_uint32(v, name, &value, errp);
}

static void cpu_max_set_sve_max_vq(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp)
{
    ARMCPU*  cpu = ARM_CPU(obj);
    uint32_t max_vq;

    if (!visit_type_uint32(v, name, &max_vq, errp)) { return; }

    if (max_vq == 0 || max_vq > ARM_MAX_VQ) {
        error_setg(errp, "unsupported SVE vector length");
        error_append_hint(errp, "Valid sve-max-vq in range [1-%d]\n", ARM_MAX_VQ);
        return;
    }

    cpu->sve_max_vq = max_vq;
}

static bool cpu_arm_get_rme(Object* obj, Error** errp)
{
    ARMCPU* cpu = ARM_CPU(obj);
    return cpu_isar_feature(aa64_rme, cpu);
}

static void cpu_arm_set_rme(Object* obj, bool value, Error** errp)
{
    ARMCPU* cpu = ARM_CPU(obj);

    FIELD_DP64_IDREG(&cpu->isar, ID_AA64PFR0, RME, value);
}

static void cpu_max_set_l0gptsz(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp)
{
    ARMCPU*  cpu = ARM_CPU(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) { return; }

    /* Encode the value for the GPCCR_EL3 field. */
    switch (value) {
        case 30:
        case 34:
        case 36:
        case 39: cpu->reset_l0gptsz = value - 30; break;
        default:
            error_setg(errp, "invalid value for l0gptsz");
            error_append_hint(errp, "valid values are 30, 34, 36, 39\n");
            break;
    }
}

static void cpu_max_get_l0gptsz(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp)
{
    ARMCPU*  cpu   = ARM_CPU(obj);
    uint32_t value = cpu->reset_l0gptsz + 30;

    visit_type_uint32(v, name, &value, errp);
}

static const Property arm_cpu_lpa2_property = DEFINE_PROP_BOOL("lpa2", ARMCPU, prop_lpa2, true);

/*
 * -cpu max: a CPU with as many features enabled as our emulation supports.
 * The version of '-cpu max' for qemu-system-arm is defined in cpu32.c;
 * this only needs to handle 64 bits.
 */
void aarch64_max_tcg_initfn(Object* obj)
{
    ARMCPU*          cpu  = ARM_CPU(obj);
    ARMISARegisters* isar = &cpu->isar;
    uint64_t         t;
    uint32_t         u;

    /*
     * Reset MIDR so the guest doesn't mistake our 'max' CPU type for a real
     * one and try to apply errata workarounds or use impdef features we
     * don't provide.
     * An IMPLEMENTER field of 0 means "reserved for software use";
     * ARCHITECTURE must be 0xf indicating "v7 or later, check ID registers
     * to see which features are present";
     * the VARIANT, PARTNUM and REVISION fields are all implementation
     * defined and we choose to define PARTNUM just in case guest
     * code needs to distinguish this QEMU CPU from other software
     * implementations, though this shouldn't be needed.
     */
    t         = REG_FIELD_DP64(0, MIDR_EL1, IMPLEMENTER, 0);
    t         = REG_FIELD_DP64(t, MIDR_EL1, ARCHITECTURE, 0xf);
    t         = REG_FIELD_DP64(t, MIDR_EL1, PARTNUM, 'Q');
    t         = REG_FIELD_DP64(t, MIDR_EL1, VARIANT, 0);
    t         = REG_FIELD_DP64(t, MIDR_EL1, REVISION, 0);
    cpu->midr = t;

    /*
     * We're going to set FEAT_S2FWB, which mandates that CLIDR_EL1.{LoUU,LoUIS}
     * are zero.
     */
    u = GET_IDREG(isar, CLIDR);
    u = REG_FIELD_DP32(u, CLIDR_EL1, LOUIS, 0);
    u = REG_FIELD_DP32(u, CLIDR_EL1, LOUU, 0);
    SET_IDREG(isar, CLIDR, u);

    /*
     * Set CTR_EL0.DIC and IDC to tell the guest it doesnt' need to
     * do any cache maintenance for data-to-instruction or
     * instruction-to-guest coherence. (Our cache ops are nops.)
     */
    t        = cpu->ctr;
    t        = REG_FIELD_DP64(t, CTR_EL0, IDC, 1);
    t        = REG_FIELD_DP64(t, CTR_EL0, DIC, 1);
    cpu->ctr = t;

    t = GET_IDREG(isar, ID_AA64ISAR0);
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, AES, 2);    /* FEAT_PMULL */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, SHA1, 1);   /* FEAT_SHA1 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, SHA2, 2);   /* FEAT_SHA512 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, CRC32, 1);  /* FEAT_CRC32 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, ATOMIC, 3); /* FEAT_LSE, FEAT_LSE128 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, RDM, 1);    /* FEAT_RDM */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, SHA3, 1);   /* FEAT_SHA3 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, SM3, 1);    /* FEAT_SM3 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, SM4, 1);    /* FEAT_SM4 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, DP, 1);     /* FEAT_DotProd */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, FHM, 1);    /* FEAT_FHM */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, TS, 2);     /* FEAT_FlagM2 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, TLB, 2);    /* FEAT_TLBIRANGE */
    t = REG_FIELD_DP64(t, ID_AA64ISAR0, RNDR, 1);   /* FEAT_RNG */
    SET_IDREG(isar, ID_AA64ISAR0, t);

    t = GET_IDREG(isar, ID_AA64ISAR1);
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, DPB, 2); /* FEAT_DPB2 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, APA, PauthFeat_FPACCOMBINED);
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, API, 1);
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, JSCVT, 1);   /* FEAT_JSCVT */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, FCMA, 1);    /* FEAT_FCMA */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, LRCPC, 2);   /* FEAT_LRCPC2 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, FRINTTS, 1); /* FEAT_FRINTTS */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, SB, 1);      /* FEAT_SB */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, SPECRES, 1); /* FEAT_SPECRES */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, BF16, 2);    /* FEAT_BF16, FEAT_EBF16 */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, DGH, 1);     /* FEAT_DGH */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, I8MM, 1);    /* FEAT_I8MM */
    t = REG_FIELD_DP64(t, ID_AA64ISAR1, XS, 1);      /* FEAT_XS */
    SET_IDREG(isar, ID_AA64ISAR1, t);

    t = GET_IDREG(isar, ID_AA64ISAR2);
    t = REG_FIELD_DP64(t, ID_AA64ISAR2, RPRES, 1); /* FEAT_RPRES */
    t = REG_FIELD_DP64(t, ID_AA64ISAR2, MOPS, 1);  /* FEAT_MOPS */
    t = REG_FIELD_DP64(t, ID_AA64ISAR2, BC, 1);    /* FEAT_HBC */
    t = REG_FIELD_DP64(t, ID_AA64ISAR2, WFXT, 2);  /* FEAT_WFxT */
    SET_IDREG(isar, ID_AA64ISAR2, t);

    t = GET_IDREG(isar, ID_AA64PFR0);
    t = REG_FIELD_DP64(t, ID_AA64PFR0, FP, 1);      /* FEAT_FP16 */
    t = REG_FIELD_DP64(t, ID_AA64PFR0, ADVSIMD, 1); /* FEAT_FP16 */
    t = REG_FIELD_DP64(t, ID_AA64PFR0, RAS, 2);     /* FEAT_RASv1p1 + FEAT_DoubleFault */
    t = REG_FIELD_DP64(t, ID_AA64PFR0, SVE, 1);
    t = REG_FIELD_DP64(t, ID_AA64PFR0, SEL2, 1); /* FEAT_SEL2 */
    t = REG_FIELD_DP64(t, ID_AA64PFR0, DIT, 1);  /* FEAT_DIT */
    t = REG_FIELD_DP64(t, ID_AA64PFR0, CSV2, 3); /* FEAT_CSV2_3 */
    t = REG_FIELD_DP64(t, ID_AA64PFR0, CSV3, 1); /* FEAT_CSV3 */
    SET_IDREG(isar, ID_AA64PFR0, t);

    t = GET_IDREG(isar, ID_AA64PFR1);
    t = REG_FIELD_DP64(t, ID_AA64PFR1, BT, 1);   /* FEAT_BTI */
    t = REG_FIELD_DP64(t, ID_AA64PFR1, SSBS, 2); /* FEAT_SSBS2 */
    /*
     * Begin with full support for MTE. This will be downgraded to MTE=0
     * during realize if the board provides no tag memory, much like
     * we do for EL2 with the virtualization=on property.
     */
    t = REG_FIELD_DP64(t, ID_AA64PFR1, MTE, 3);       /* FEAT_MTE3 */
    t = REG_FIELD_DP64(t, ID_AA64PFR1, RAS_FRAC, 0);  /* FEAT_RASv1p1 + FEAT_DoubleFault */
    t = REG_FIELD_DP64(t, ID_AA64PFR1, SME, 2);       /* FEAT_SME2 */
    t = REG_FIELD_DP64(t, ID_AA64PFR1, CSV2_FRAC, 0); /* FEAT_CSV2_3 */
    t = REG_FIELD_DP64(t, ID_AA64PFR1, NMI, 1);       /* FEAT_NMI */
    SET_IDREG(isar, ID_AA64PFR1, t);

    t = GET_IDREG(isar, ID_AA64MMFR0);
    t = REG_FIELD_DP64(t, ID_AA64MMFR0, PARANGE, 6);   /* FEAT_LPA: 52 bits */
    t = REG_FIELD_DP64(t, ID_AA64MMFR0, TGRAN16, 1);   /* 16k pages supported */
    t = REG_FIELD_DP64(t, ID_AA64MMFR0, TGRAN16_2, 2); /* 16k stage2 supported */
    t = REG_FIELD_DP64(t, ID_AA64MMFR0, TGRAN64_2, 2); /* 64k stage2 supported */
    t = REG_FIELD_DP64(t, ID_AA64MMFR0, TGRAN4_2, 2);  /*  4k stage2 supported */
    t = REG_FIELD_DP64(t, ID_AA64MMFR0, FGT, 1);       /* FEAT_FGT */
    t = REG_FIELD_DP64(t, ID_AA64MMFR0, ECV, 2);       /* FEAT_ECV */
    SET_IDREG(isar, ID_AA64MMFR0, t);

    t = GET_IDREG(isar, ID_AA64MMFR1);
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, HAFDBS, 2);   /* FEAT_HAFDBS */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, VMIDBITS, 2); /* FEAT_VMID16 */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, VH, 1);       /* FEAT_VHE */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, HPDS, 2);     /* FEAT_HPDS2 */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, LO, 1);       /* FEAT_LOR */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, PAN, 3);      /* FEAT_PAN3 */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, XNX, 1);      /* FEAT_XNX */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, ETS, 2);      /* FEAT_ETS2 */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, HCX, 1);      /* FEAT_HCX */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, AFP, 1);      /* FEAT_AFP */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, TIDCP1, 1);   /* FEAT_TIDCP1 */
    t = REG_FIELD_DP64(t, ID_AA64MMFR1, CMOW, 1);     /* FEAT_CMOW */
    SET_IDREG(isar, ID_AA64MMFR1, t);

    t = GET_IDREG(isar, ID_AA64MMFR2);
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, CNP, 1);     /* FEAT_TTCNP */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, UAO, 1);     /* FEAT_UAO */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, IESB, 1);    /* FEAT_IESB */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, VARANGE, 1); /* FEAT_LVA */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, NV, 2);      /* FEAT_NV2 */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, ST, 1);      /* FEAT_TTST */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, AT, 1);      /* FEAT_LSE2 */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, IDS, 1);     /* FEAT_IDST */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, FWB, 1);     /* FEAT_S2FWB */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, TTL, 1);     /* FEAT_TTL */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, BBM, 2);     /* FEAT_BBM at level 2 */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, EVT, 2);     /* FEAT_EVT */
    t = REG_FIELD_DP64(t, ID_AA64MMFR2, E0PD, 1);    /* FEAT_E0PD */
    SET_IDREG(isar, ID_AA64MMFR2, t);

    FIELD_DP64_IDREG(isar, ID_AA64MMFR3, SPEC_FPACC, 1); /* FEAT_FPACC_SPEC */

    t = GET_IDREG(isar, ID_AA64ZFR0);
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, SVEVER, 2);   /* FEAT_SVE2p1 */
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, AES, 2);      /* FEAT_SVE_PMULL128 */
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, BITPERM, 1);  /* FEAT_SVE_BitPerm */
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, BFLOAT16, 2); /* FEAT_BF16, FEAT_EBF16 */
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, B16B16, 1);   /* FEAT_SVE_B16B16 */
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, SHA3, 1);     /* FEAT_SVE_SHA3 */
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, SM4, 1);      /* FEAT_SVE_SM4 */
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, I8MM, 1);     /* FEAT_I8MM */
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, F32MM, 1);    /* FEAT_F32MM */
    t = REG_FIELD_DP64(t, ID_AA64ZFR0, F64MM, 1);    /* FEAT_F64MM */
    SET_IDREG(isar, ID_AA64ZFR0, t);

    t = GET_IDREG(isar, ID_AA64DFR0);
    t = REG_FIELD_DP64(t, ID_AA64DFR0, DEBUGVER, 10); /* FEAT_Debugv8p8 */
    t = REG_FIELD_DP64(t, ID_AA64DFR0, PMUVER, 6);    /* FEAT_PMUv3p5 */
    t = REG_FIELD_DP64(t, ID_AA64DFR0, HPMN0, 1);     /* FEAT_HPMN0 */
    SET_IDREG(isar, ID_AA64DFR0, t);

    t = GET_IDREG(isar, ID_AA64SMFR0);
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, F32F32, 1);   /* FEAT_SME */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, BI32I32, 1);  /* FEAT_SME2 */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, B16F32, 1);   /* FEAT_SME */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, F16F32, 1);   /* FEAT_SME */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, I8I32, 0xf);  /* FEAT_SME */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, F16F16, 1);   /* FEAT_SME_F16F16 */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, B16B16, 1);   /* FEAT_SME_B16B16 */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, I16I32, 5);   /* FEAT_SME2 */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, F64F64, 1);   /* FEAT_SME_F64F64 */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, I16I64, 0xf); /* FEAT_SME_I16I64 */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, SMEVER, 2);   /* FEAT_SME2p1 */
    t = REG_FIELD_DP64(t, ID_AA64SMFR0, FA64, 1);     /* FEAT_SME_FA64 */
    SET_IDREG(isar, ID_AA64SMFR0, t);

    /* Replicate the same data to the 32-bit id registers.  */
    aa32_max_features(cpu);

    cpu->gm_blocksize = 6; /*  256 bytes */

    cpu->sve_vq.supported = MAKE_64BIT_MASK(0, ARM_MAX_VQ);
    cpu->sme_vq.supported = SVE_VQ_POW2_MAP;

    aarch64_add_pauth_properties(obj);
    aarch64_add_sve_properties(obj);
    aarch64_add_sme_properties(obj);
    object_property_add(obj, "sve-max-vq", "uint32", cpu_max_get_sve_max_vq, cpu_max_set_sve_max_vq, NULL, NULL);
    object_property_add_bool(obj, "x-rme", cpu_arm_get_rme, cpu_arm_set_rme);
    object_property_add(obj, "x-l0gptsz", "uint32", cpu_max_get_l0gptsz, cpu_max_set_l0gptsz, NULL, NULL);
    qdev_property_add_static(DEVICE(obj), &arm_cpu_lpa2_property);
}
