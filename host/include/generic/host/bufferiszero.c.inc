/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * buffer_is_zero acceleration, generic version.
 */

static const biz_accel_fn accel_table[1] = {buffer_is_zero_int_ge256};

#define best_accel() 0
