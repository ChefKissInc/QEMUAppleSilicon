/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper file for declaring TCG helper functions.
 * This one expands prototypes for the helper functions.
 */

#pragma once

#include "exec/helper-proto-common.h"

#define HELPER_H "helper.h"
#include "exec/helper-proto.h.inc"
#undef HELPER_H
