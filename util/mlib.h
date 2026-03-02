/*
 * M*LIB Include Wrapper.
 *
 * Copyright (c) 2026 Visual Ehrmanntraut (VisualEhrmanntraut).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef UTIL_MLIB_H

// Speed up compilation and runtime speed.
#ifndef DEBUG
#define M_ASSERT(expr) ((expr) ? (void)0 : __builtin_unreachable())
#define M_ASSERT_SLOW(expr) ((expr) ? (void)0 : __builtin_unreachable())
#endif

#include "mlib/m-algo.h"
#include "mlib/m-array.h"
#include "mlib/m-dict.h"

#endif /* UTIL_MLIB_H */
