/*
 * API for host PCI MMIO accesses (e.g. Linux VFIO BARs)
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Farhan Ali <alifm@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "qemu/bswap.h"

static inline uint8_t host_pci_ldub_p(const void *ioaddr)
{
    uint8_t ret = 0;

    ret = ldub_p(ioaddr);

    return ret;
}

static inline uint16_t host_pci_lduw_le_p(const void *ioaddr)
{
    uint16_t ret = 0;

    ret = lduw_le_p(ioaddr);

    return ret;
}

static inline uint32_t host_pci_ldl_le_p(const void *ioaddr)
{
    uint32_t ret = 0;

    ret = ldl_le_p(ioaddr);

    return ret;
}

static inline uint64_t host_pci_ldq_le_p(const void *ioaddr)
{
    uint64_t ret = 0;

    ret = ldq_le_p(ioaddr);

    return ret;
}

static inline void host_pci_stb_p(void *ioaddr, uint8_t val)
{
    stb_p(ioaddr, val);
}

static inline void host_pci_stw_le_p(void *ioaddr, uint16_t val)
{
    stw_le_p(ioaddr, val);
}

static inline void host_pci_stl_le_p(void *ioaddr, uint32_t val)
{
    stl_le_p(ioaddr, val);
}

static inline void host_pci_stq_le_p(void *ioaddr, uint64_t val)
{
    stq_le_p(ioaddr, val);
}

static inline uint64_t host_pci_ldn_le_p(const void *ioaddr, int sz)
{
    switch (sz) {
    case 1:
        return host_pci_ldub_p(ioaddr);
    case 2:
        return host_pci_lduw_le_p(ioaddr);
    case 4:
        return host_pci_ldl_le_p(ioaddr);
    case 8:
        return host_pci_ldq_le_p(ioaddr);
    default:
        assert_not_reached();
    }
}

static inline void host_pci_stn_le_p(void *ioaddr, int sz, uint64_t v)
{
    switch (sz) {
    case 1:
        host_pci_stb_p(ioaddr, v);
        break;
    case 2:
        host_pci_stw_le_p(ioaddr, v);
        break;
    case 4:
        host_pci_stl_le_p(ioaddr, v);
        break;
    case 8:
        host_pci_stq_le_p(ioaddr, v);
        break;
    default:
        assert_not_reached();
    }
}
