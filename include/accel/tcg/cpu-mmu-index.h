/*
 * cpu_mmu_index()
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "hw/core/cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "tcg/debug-assert.h"

/**
 * cpu_mmu_index:
 * @env: The cpu environment
 * @ifetch: True for code access, false for data access.
 *
 * Return the core mmu index for the current translation regime.
 * This function is used by generic TCG code paths.
 */
static inline int cpu_mmu_index(CPUState* cs, bool ifetch)
{
    int ret = cs->cc->tcg_ops->mmu_index(cs, ifetch);
    tcg_debug_assert(ret >= 0 && ret < NB_MMU_MODES);
    return ret;
}
