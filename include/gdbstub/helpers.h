/*
 * gdbstub helpers
 *
 * These are all used by the various frontends and have to be host
 * aware to ensure things are store in target order.
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _GDBSTUB_HELPERS_H_
#define _GDBSTUB_HELPERS_H_

#ifndef COMPILING_PER_TARGET
    #error "gdbstub helpers should only be included by target specific code"
#endif

#include "qemu/bswap.h"
#include "qemu/target-info.h"

/*
 * The GDB remote protocol transfers values in target byte order. As
 * the gdbstub may be batching up several register values we always
 * append to the array.
 */

static inline int gdb_get_reg32(GByteArray* buf, uint32_t val)
{
    if (target_big_endian()) { cpu_to_be32s(&val); }
    else {
        cpu_to_le32s(&val);
    }
    g_byte_array_append(buf, (uint8_t*)&val, 4);
    return 4;
}

static inline int gdb_get_reg64(GByteArray* buf, uint64_t val)
{
    if (target_big_endian()) { cpu_to_be64s(&val); }
    else {
        cpu_to_le64s(&val);
    }
    g_byte_array_append(buf, (uint8_t*)&val, 8);
    return 8;
}

static inline int gdb_get_reg128(GByteArray* buf, uint64_t val_hi, uint64_t val_lo)
{
    uint64_t tmp[2];
    if (target_big_endian()) {
        tmp[0] = cpu_to_be64(val_hi);
        tmp[1] = cpu_to_be64(val_lo);
    }
    else {
        tmp[0] = cpu_to_le64(val_lo);
        tmp[1] = cpu_to_le64(val_hi);
    }
    g_byte_array_append(buf, (uint8_t*)&tmp, 16);
    return 16;
}

#endif /* _GDBSTUB_HELPERS_H_ */
