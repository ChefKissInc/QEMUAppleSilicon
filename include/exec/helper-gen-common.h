/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper file for declaring TCG helper functions.
 * This one expands generation functions for tcg opcodes.
 */

#pragma once

#define HELPER_H "accel/tcg/tcg-runtime.h"
#include "exec/helper-gen.h.inc"
#undef HELPER_H
