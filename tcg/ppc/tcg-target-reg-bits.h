/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2008 Fabrice Bellard
 */

#pragma once

#ifdef _ARCH_PPC64
    #define TCG_TARGET_REG_BITS 64
#else
    #define TCG_TARGET_REG_BITS 32
#endif
