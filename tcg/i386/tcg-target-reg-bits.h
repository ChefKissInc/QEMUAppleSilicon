/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2008 Fabrice Bellard
 */

#pragma once

#ifdef __x86_64__
    #define TCG_TARGET_REG_BITS 64
#else
    #define TCG_TARGET_REG_BITS 32
#endif
