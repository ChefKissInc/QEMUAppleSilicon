/*
 * Accelerator interface, specializes CPUClass
 *
 * Copyright 2021 SUSE LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "qom/object.h"
#include "hw/core/cpu.h"

typedef struct AccelCPUClass
{
    ObjectClass parent_class;

    void (*cpu_class_init)(CPUClass* cc);
    void (*cpu_instance_init)(CPUState* cpu);
    bool (*cpu_target_realize)(CPUState* cpu, Error** errp);
} AccelCPUClass;
