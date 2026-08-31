/*
 * GDB session exit handling
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

/**
 * gdb_exit: exit gdb session, reporting inferior status
 * @code: exit code reported
 *
 * This closes the session and sends a final packet to GDB reporting
 * the exit status of the program. It also cleans up any connections
 * detritus before returning.
 */
void gdb_exit(int code);

/**
 * gdb_qemu_exit: ask qemu to exit
 * @code: exit code reported
 *
 * This requests qemu to exit. This function is allowed to return as
 * the exit request might be processed asynchronously by qemu backend.
 */
void gdb_qemu_exit(int code);
