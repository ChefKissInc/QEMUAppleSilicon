/*
 * TCP Remote USB Host.
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

#ifndef HW_USB_HCD_TCP_H
#define HW_USB_HCD_TCP_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/usb.h"
#include "hw/usb/tcp-usb.h"
#include "io/channel.h"
#include "qemu/coroutine.h"
#include "qom/object.h"

#define TYPE_USB_TCP_HOST "usb-tcp-host"
OBJECT_DECLARE_SIMPLE_TYPE(USBTCPHostState, USB_TCP_HOST)

typedef struct USBTCPPacket {
    USBPacket p;
    void *buffer;
    USBDevice *dev;
    USBTCPHostState *s;
    uint8_t addr;
} USBTCPPacket;

struct USBTCPHostState {
    SysBusDevice parent_obj;

    USBBus bus;
    USBPort ports[3];
    QIOChannel *ioc;
    CoMutex write_mutex;
    Error *migration_blocker;
    bool closed;
    bool stopped;
    USBTCPRemoteConnType conn_type;
    char *conn_addr;
    uint16_t conn_port;
};

#endif /* HW_USB_HCD_TCP_H */
