/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Atomic store insert into 128-bit, LoongArch version.
 */

#pragma once

void store_atom_insert_al16(Int128* ps, Int128 val, Int128 msk) QEMU_ERROR("unsupported atomic");
