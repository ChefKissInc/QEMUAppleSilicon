/*
 * ChefKiss Patcher (PenguinWizardryC).
 *
 * Copyright (c) 2025-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
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

#ifndef HW_ARM_APPLE_SILICON_PATCHER_H
#define HW_ARM_APPLE_SILICON_PATCHER_H

#include "qemu/osdep.h"
#include "exec/vaddr.h"

typedef struct {
    /// Physical or virtual address.
    vaddr addr;
    vaddr length;
    /// Guaranteed to be an accessible host pointer.
    void *ptr;
    const char *name;
} CKPatcherRange;

/// Precondition: `insn` must be masked.
void *ck_patcher_find_next_insn(void *buffer, uint32_t num, uint32_t insn,
                                uint32_t mask, uint32_t skip);
/// See `ck_patcher_find_next_insn`.
void *ck_patcher_find_prev_insn(void *buffer, uint32_t num, uint32_t insn,
                                uint32_t mask, uint32_t skip);

/// Callback function prototype. `ctx` may be null.
typedef bool (*CKPatcherCallback)(void *ctx, uint8_t *buffer);

/// Precondition: bytes in `find` must be masked.
/// If `align` is set to a non-zero value, the searching will be aligned
/// to its value amount of bytes, otherwise it will align the search by
/// a single byte.
bool ck_patcher_find_callback_ctx(CKPatcherRange *range, const char *name,
                                  const uint8_t *pattern, const uint8_t *mask,
                                  size_t len, size_t align, void *ctx,
                                  CKPatcherCallback callback);
/// See `ck_patcher_find_callback_ctx`.
bool ck_patcher_find_callback(CKPatcherRange *range, const char *name,
                              const uint8_t *pattern, const uint8_t *mask,
                              size_t len, size_t align,
                              CKPatcherCallback callback);
/// See `ck_patcher_find_callback`.
/// `replace_off` is the byte offset in the matched pattern
/// which the `replacement` will be applied on.
bool ck_patcher_find_replace(CKPatcherRange *range, const char *name,
                             const uint8_t *pattern, const uint8_t *mask,
                             size_t len, size_t align,
                             const uint8_t *replacement,
                             const uint8_t *replacement_mask,
                             size_t replace_off, size_t replace_len);

#endif /* HW_ARM_APPLE_SILICON_PATCHER_H */
