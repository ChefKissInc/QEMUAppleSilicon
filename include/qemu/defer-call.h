/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Deferred calls
 *
 * Copyright Red Hat.
 */

#pragma once

/* See documentation in util/defer-call.c */
void defer_call_begin(void);
void defer_call_end(void);
void defer_call(void (*fn)(void*), void* opaque);
