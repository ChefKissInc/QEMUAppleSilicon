/*
 * ARM cpu parameters for qemu.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#ifdef TARGET_AARCH64
    #define TARGET_PHYS_ADDR_SPACE_BITS 52
    #define TARGET_VIRT_ADDR_SPACE_BITS 52
#else
    #define TARGET_PHYS_ADDR_SPACE_BITS 40
    #define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif

/*
 * ARMv7 and later CPUs have 4K pages minimum, but ARMv5 and v6
 * have to support 1K tiny pages.
 */
#define TARGET_PAGE_BITS_VARY
#define TARGET_PAGE_BITS_LEGACY 10

/*
 * ARM-specific extra insn start words:
 * 1: Conditional execution bits
 * 2: Partial exception syndrome for data aborts
 */
#define TARGET_INSN_START_EXTRA_WORDS 2
