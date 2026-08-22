/*
 * No host specific carry-less multiply acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#define HAVE_CLMUL_ACCEL false
#define ATTR_CLMUL_ACCEL

Int128 clmul_64_accel(uint64_t, uint64_t) QEMU_ERROR("unsupported accel");
