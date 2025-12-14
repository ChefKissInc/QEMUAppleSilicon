/*
 * TCP Remote USB.
 *
 * Copyright (c) 2023-2025 Visual Ehrmanntraut.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_USB_TCP_USB_H
#define HW_USB_TCP_USB_H

#include "qemu/osdep.h"
#include "qapi/util.h"

#define USB_TCP_REMOTE_UNIX_DEFAULT ("/tmp/QEMUASUSBRemote")

typedef enum {
    TCP_REMOTE_CONN_TYPE_UNIX,
    TCP_REMOTE_CONN_TYPE_IPV4,
    TCP_REMOTE_CONN_TYPE_IPV6,
    TCP_REMOTE_CONN_TYPE__MAX,
} USBTCPRemoteConnType;

extern const QEnumLookup USBTCPRemoteConnType_lookup;
extern const PropertyInfo qdev_usb_tcp_remote_conn_type;

#define DEFINE_PROP_USB_TCP_REMOTE_CONN_TYPE(_name, _state, _fld, _default) \
    DEFINE_PROP_UNSIGNED(_name, _state, _fld, _default,                     \
                         qdev_usb_tcp_remote_conn_type, USBTCPRemoteConnType)

enum {
    TCP_USB_REQUEST = 1,
    TCP_USB_RESPONSE,
    TCP_USB_RESET,
    TCP_USB_CANCEL
};

typedef struct QEMU_PACKED tcp_usb_header {
    uint8_t type;
} tcp_usb_header_t;

typedef struct QEMU_PACKED tcp_usb_request_header {
    uint8_t addr;
    int pid;
    uint8_t ep;
    uint64_t id;
    unsigned int stream;
    uint8_t short_not_ok;
    uint8_t int_req;
    uint16_t length;
} tcp_usb_request_header;

typedef struct QEMU_PACKED tcp_usb_response_header {
    uint8_t addr;
    int pid;
    uint8_t ep;
    uint64_t id;
    uint32_t status;
    uint16_t length;
} tcp_usb_response_header;

typedef struct QEMU_PACKED tcp_usb_cancel_header {
    uint8_t addr;
    int pid;
    uint8_t ep;
    uint64_t id;
} tcp_usb_cancel_header;

#endif /* HW_USB_TCP_USB_H */
