/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 */

#pragma once

#if _MIPS_SIM == _ABIO32
# define TCG_TARGET_REG_BITS 32
#elif _MIPS_SIM == _ABIN32 || _MIPS_SIM == _ABI64
# define TCG_TARGET_REG_BITS 64
#else
# error "Unknown ABI"
#endif
