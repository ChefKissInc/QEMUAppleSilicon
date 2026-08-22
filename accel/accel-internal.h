/*
 * QEMU accel internal functions
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#pragma once

#include "qemu/accel.h"

void accel_init_ops_interfaces(AccelClass *ac);

/* ACCEL_SYSTEM_H */
