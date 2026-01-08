/*
 * TCP Remote USB.
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

#ifndef HW_USB_DEV_TCP_REMOTE_H
#define HW_USB_DEV_TCP_REMOTE_H

#include "qemu/osdep.h"
#include "hw/usb.h"
#include "hw/usb/tcp-usb.h"

typedef struct USBTCPInflightPacket {
    USBPacket *p;
    uint64_t handled;
    QTAILQ_ENTRY(USBTCPInflightPacket) queue;
    uint8_t addr;
} USBTCPInflightPacket;

typedef struct USBTCPCompletedPacket {
    USBPacket *p;
    QTAILQ_ENTRY(USBTCPCompletedPacket) queue;
    uint8_t addr;
} USBTCPCompletedPacket;

struct USBTCPRemoteState {
    USBDevice parent_obj;

    QemuThread thread;
    QemuThread read_thread;
    QemuCond cond;
    QemuMutex thr_mutex;
    QemuMutex request_mutex;

    QemuMutex queue_mutex;
    QTAILQ_HEAD(, USBTCPInflightPacket) queue;

    QemuMutex completed_queue_mutex;
    QemuCond completed_queue_cond;
    QTAILQ_HEAD(, USBTCPCompletedPacket) completed_queue;
    QEMUBH *completed_bh;
    QEMUBH *addr_bh;
    QEMUBH *cleanup_bh;
    Error *migration_blocker;

    USBTCPRemoteConnType conn_type;
    char *conn_addr;
    uint16_t conn_port;
    int socket;
    int fd;
    uint8_t addr;
    bool closed;
    bool stopped;
};

#define TYPE_USB_TCP_REMOTE "usb-tcp-remote"
OBJECT_DECLARE_SIMPLE_TYPE(USBTCPRemoteState, USB_TCP_REMOTE)

#endif /* HW_USB_DEV_TCP_REMOTE_H */
