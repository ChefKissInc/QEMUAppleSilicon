/*
 * QEMU abi_ptr type definitions
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include "cpu-param.h"
#include "exec/target_long.h"

typedef target_ulong abi_ptr;
#define TARGET_ABI_FMT_ptr TARGET_FMT_lx
