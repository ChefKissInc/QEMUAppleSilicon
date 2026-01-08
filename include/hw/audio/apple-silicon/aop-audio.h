/*
 * Apple Always-On Processor: Audio.
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

#ifndef HW_AUDIO_APPLE_SILICON_AOP_AUDIO_H
#define HW_AUDIO_APPLE_SILICON_AOP_AUDIO_H

#include "qemu/osdep.h"
#include "hw/misc/apple-silicon/aop.h"
#include "qom/object.h"

#define TYPE_APPLE_AOP_AUDIO "apple-aop-audio"
OBJECT_DECLARE_SIMPLE_TYPE(AppleAOPAudioState, APPLE_AOP_AUDIO)

SysBusDevice *apple_aop_audio_create(AppleAOPState *aop);

#endif /* HW_AUDIO_APPLE_SILICON_AOP_AUDIO_H */
