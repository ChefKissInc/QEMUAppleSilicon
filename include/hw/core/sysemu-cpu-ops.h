/*
 * CPU operations specific to system emulation
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSTEM_CPU_OPS_H
#define SYSTEM_CPU_OPS_H

#include "hw/core/cpu.h"

/*
 * struct SysemuCPUOps: System operations specific to a CPU class
 */
typedef struct SysemuCPUOps {
    /**
     * @has_work: Callback for checking if there is work to do.
     */
    bool (*has_work)(CPUState *cpu); /* MANDATORY NON-NULL */
    /**
     * @get_memory_mapping: Callback for obtaining the memory mappings.
     */
    bool (*get_memory_mapping)(CPUState *cpu, MemoryMappingList *list,
                               Error **errp);
    /**
     * @get_paging_enabled: Callback for inquiring whether paging is enabled.
     */
    bool (*get_paging_enabled)(const CPUState *cpu);
    /**
     * @get_phys_page_debug: Callback for obtaining a physical address.
     */
    hwaddr (*get_phys_page_debug)(CPUState *cpu, vaddr addr);
    /**
     * @get_phys_page_attrs_debug: Callback for obtaining a physical address
     *       and the associated memory transaction attributes to use for the
     *       access.
     * CPUs which use memory transaction attributes should implement this
     * instead of get_phys_page_debug.
     */
    hwaddr (*get_phys_page_attrs_debug)(CPUState *cpu, vaddr addr,
                                        MemTxAttrs *attrs);
    /**
     * @asidx_from_attrs: Callback to return the CPU AddressSpace to use for
     *       a memory access with the specified memory transaction attributes.
     */
    int (*asidx_from_attrs)(CPUState *cpu, MemTxAttrs attrs);
    /**
     * @get_crash_info: Callback for reporting guest crash information in
     * GUEST_PANICKED events.
     */
    GuestPanicInformation* (*get_crash_info)(CPUState *cpu);
} SysemuCPUOps;

#endif /* SYSTEM_CPU_OPS_H */
