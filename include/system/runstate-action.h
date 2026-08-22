/*
 * Copyright (c) 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#pragma once

#include "qapi/qapi-commands-run-state.h"

/* in system/runstate-action.c */
extern RebootAction reboot_action;
extern ShutdownAction shutdown_action;
extern PanicAction panic_action;
