/*
 * QEMU Arm software mmu index definitions
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "mmuidx-internal.h"

#define EL(X)  ((X << R_MMUIDXINFO_EL_SHIFT) | R_MMUIDXINFO_ELVALID_MASK | ((X == 0) << R_MMUIDXINFO_USER_SHIFT))
#define REL(X) ((X << R_MMUIDXINFO_REL_SHIFT) | R_MMUIDXINFO_RELVALID_MASK)
#define R2     R_MMUIDXINFO_2RANGES_MASK
#define PAN    R_MMUIDXINFO_PAN_MASK
#define USER   R_MMUIDXINFO_USER_MASK
#define S1     R_MMUIDXINFO_STAGE1_MASK
#define S2     R_MMUIDXINFO_STAGE2_MASK

const uint32_t arm_mmuidx_table[ARM_MMU_IDX_NOTLB + 8] = {
    [ARMMMUIdx_E10_0]     = EL(0) | REL(1) | R2,
    [ARMMMUIdx_E10_1]     = EL(1) | REL(1) | R2,
    [ARMMMUIdx_E10_1_PAN] = EL(1) | REL(1) | R2 | PAN,

    [ARMMMUIdx_E20_0]     = EL(0) | REL(2) | R2,
    [ARMMMUIdx_E20_2]     = EL(2) | REL(2) | R2,
    [ARMMMUIdx_E20_2_PAN] = EL(2) | REL(2) | R2 | PAN,

    [ARMMMUIdx_E2] = EL(2) | REL(2),

    [ARMMMUIdx_E3]        = EL(3) | REL(3),
    [ARMMMUIdx_E30_0]     = EL(0) | REL(3),
    [ARMMMUIdx_E30_3_PAN] = EL(3) | REL(3) | PAN,

    [ARMMMUIdx_Stage2_S] = REL(2) | S2,
    [ARMMMUIdx_Stage2]   = REL(2) | S2,

    /*
     * GXF variants mirror the properties of the index they are derived
     * from; the guarded state itself is carried by ARM_MMU_IDX_A_GXF.
     */
    [ARMMMUIdx_GE10_1]     = EL(1) | REL(1) | R2,
    [ARMMMUIdx_GE10_1_PAN] = EL(1) | REL(1) | R2 | PAN,
    [ARMMMUIdx_GE20_2]     = EL(2) | REL(2) | R2,
    [ARMMMUIdx_GE20_2_PAN] = EL(2) | REL(2) | R2 | PAN,
    [ARMMMUIdx_GE2]        = EL(2) | REL(2),
    [ARMMMUIdx_GE3]        = EL(3) | REL(3),
    [ARMMMUIdx_GE30_3_PAN] = EL(3) | REL(3) | PAN,

    [ARMMMUIdx_Stage1_E0]      = REL(1) | R2 | S1 | USER,
    [ARMMMUIdx_Stage1_E1]      = REL(1) | R2 | S1,
    [ARMMMUIdx_Stage1_E1_PAN]  = REL(1) | R2 | S1 | PAN,
    [ARMMMUIdx_Stage1_GE1]     = REL(1) | R2 | S1,
    [ARMMMUIdx_Stage1_GE1_PAN] = REL(1) | R2 | S1 | PAN,
};
