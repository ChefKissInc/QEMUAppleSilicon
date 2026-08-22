/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Definition of TCGTBCPUState.
 */

#pragma once

#include "exec/vaddr.h"

typedef struct TCGTBCPUState
{
    vaddr    pc;
    uint32_t flags;
    uint32_t cflags;
    uint64_t cs_base;
} TCGTBCPUState;
