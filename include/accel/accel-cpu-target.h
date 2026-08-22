/*
 * Accelerator interface, specializes CPUClass
 * This header is used only by target-specific code.
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#pragma once

/*
 * This header is used to define new accelerator-specific target-specific
 * accelerator cpu subclasses.
 * It uses CPU_RESOLVING_TYPE, so this is clearly target-specific.
 *
 * Do not try to use for any other purpose than the implementation of new
 * subclasses in target/, or the accel implementation itself in accel/
 */

#include "qom/object.h"
#include "accel/accel-cpu.h"
#include "cpu.h"

#define TYPE_ACCEL_CPU "accel-" CPU_RESOLVING_TYPE
#define ACCEL_CPU_NAME(name) (name "-" TYPE_ACCEL_CPU)
DECLARE_CLASS_CHECKERS(AccelCPUClass, ACCEL_CPU, TYPE_ACCEL_CPU)

/* ACCEL_CPU_H */
