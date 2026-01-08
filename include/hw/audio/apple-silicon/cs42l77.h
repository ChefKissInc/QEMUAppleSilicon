/*
 * Apple CS42L77 Amp.
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

#ifndef HW_MISC_APPLE_SILICON_CS42L77_H
#define HW_MISC_APPLE_SILICON_CS42L77_H

#include "qemu/osdep.h"
#include "qom/object.h"

#define TYPE_APPLE_CS42L77 "apple-cs42l77"
OBJECT_DECLARE_SIMPLE_TYPE(AppleCS42L77State, APPLE_CS42L77);

#endif /* HW_MISC_APPLE_SILICON_CS42L77_H */
