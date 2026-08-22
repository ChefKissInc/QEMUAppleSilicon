/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2009, 2011 Stefan Weil
 */

#pragma once

#if UINTPTR_MAX == UINT32_MAX
    #define TCG_TARGET_REG_BITS 32
#elif UINTPTR_MAX == UINT64_MAX
    #define TCG_TARGET_REG_BITS 64
#else
    #error Unknown pointer size for tci target
#endif
