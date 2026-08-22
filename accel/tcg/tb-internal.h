/*
 * TranslationBlock internal declarations (target specific)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "exec/translation-block.h"

/*
 * The true return address will often point to a host insn that is part of
 * the next translated guest insn.  Adjust the address backward to point to
 * the middle of the call insn.  Subtracting one would do the job except for
 * several compressed mode architectures (arm, mips) which set the low bit
 * to indicate the compressed mode; subtracting two works around that.  It
 * is also the case that there are no host isas that contain a call insn
 * smaller than 4 bytes, so we don't worry about special-casing this.
 */
#define GETPC_ADJ 2

void tb_lock_page0(tb_page_addr_t);
void tb_lock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_pages(TranslationBlock*);

#ifdef CONFIG_SOFTMMU
void tb_invalidate_phys_range_fast(CPUState* cpu, ram_addr_t ram_addr, unsigned size, uintptr_t retaddr);
#endif /* CONFIG_SOFTMMU */

bool tb_invalidate_phys_page_unwind(CPUState* cpu, tb_page_addr_t addr, uintptr_t pc);
