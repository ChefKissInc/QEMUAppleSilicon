/*
 * TCG IOMMU translations.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include "exec/hwaddr.h"
#include "exec/memattrs.h"

MemoryRegionSection *address_space_translate_for_iotlb(CPUState *cpu,
                                                       int asidx,
                                                       hwaddr addr,
                                                       hwaddr *xlat,
                                                       hwaddr *plen,
                                                       MemTxAttrs attrs,
                                                       int *prot);
