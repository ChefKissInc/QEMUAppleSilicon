/*
 * TCP Remote USB.
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

#include "qemu/osdep.h"
#include "hw/qdev-properties.h" // do not reorder this
#include "hw/core/qdev-prop-internal.h"
#include "hw/usb/tcp-usb.h"

const QEnumLookup USBTCPRemoteConnType_lookup = {
    .array =
        (const char *const[]){
            [TCP_REMOTE_CONN_TYPE_UNIX] = "unix",
            [TCP_REMOTE_CONN_TYPE_IPV4] = "ipv4",
            [TCP_REMOTE_CONN_TYPE_IPV6] = "ipv6",
        },
    .size = TCP_REMOTE_CONN_TYPE__MAX,
};

const PropertyInfo qdev_usb_tcp_remote_conn_type = {
    .type = "USBTCPRemoteConnType",
    .description = "unix/ipv4/ipv6",
    .enum_table = &USBTCPRemoteConnType_lookup,
    .get = qdev_propinfo_get_enum,
    .set = qdev_propinfo_set_enum,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};
