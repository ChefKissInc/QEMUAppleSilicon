/*
 * Apple RTKit.
 *
 * Copyright (c) 2023-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
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

#ifndef HW_MISC_APPLE_SILICON_A7IOP_RTKIT_H
#define HW_MISC_APPLE_SILICON_A7IOP_RTKIT_H

#include "qemu/osdep.h"
#include "hw/misc/apple-silicon/a7iop/core.h"

#define TYPE_APPLE_RTKIT "apple-rtkit"
OBJECT_DECLARE_TYPE(AppleRTKit, AppleRTKitClass, APPLE_RTKIT)

typedef void AppleRTKitEPHandler(void *opaque, uint8_t ep, uint64_t msg);

typedef struct {
    void *opaque;
    AppleRTKitEPHandler *handler;
    bool user;
} AppleRTKitEPData;

typedef struct {
    void (*start)(void *opaque);
    void (*wakeup)(void *opaque);
    void (*boot_done)(void *opaque);
} AppleRTKitOps;

struct AppleRTKitClass {
    /*< private >*/
    SysBusDevice base_class;

    /*< public >*/
    ResettablePhases parent_phases;
};

struct AppleRTKit {
    /*< private >*/
    AppleA7IOP parent_obj;

    /*< public >*/
    const AppleRTKitOps *ops;
    QemuMutex lock;
    void *opaque;
    uint8_t ep0_status;
    uint32_t protocol_version;
    GHashTable *endpoints;
    QTAILQ_HEAD(, AppleA7IOPMessage) rollcall;
};

void apple_rtkit_send_control_msg(AppleRTKit *s, uint8_t ep, uint64_t data);
void apple_rtkit_send_user_msg(AppleRTKit *s, uint8_t ep, uint64_t data);
void apple_rtkit_register_control_ep(AppleRTKit *s, uint8_t ep, void *opaque,
                                     AppleRTKitEPHandler *handler);
void apple_rtkit_register_user_ep(AppleRTKit *s, uint8_t ep, void *opaque,
                                  AppleRTKitEPHandler *handler);
void apple_rtkit_unregister_control_ep(AppleRTKit *s, uint8_t ep);
void apple_rtkit_unregister_user_ep(AppleRTKit *s, uint8_t ep);
void apple_rtkit_init(AppleRTKit *s, void *opaque, const char *role,
                      uint64_t mmio_size, AppleA7IOPVersion version,
                      const AppleRTKitOps *ops);
AppleRTKit *apple_rtkit_new(void *opaque, const char *role, uint64_t mmio_size,
                            AppleA7IOPVersion version,
                            const AppleRTKitOps *ops);

extern const VMStateDescription vmstate_apple_rtkit;

#define VMSTATE_APPLE_RTKIT(_field, _state) \
    VMSTATE_STRUCT(_field, _state, 0, vmstate_apple_rtkit, AppleRTKit)

#endif /* HW_MISC_APPLE_SILICON_A7IOP_RTKIT_H */
