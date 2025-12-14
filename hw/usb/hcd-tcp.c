/*
 * TCP Remote USB Host.
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

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "hw/usb/hcd-tcp.h"
#include "io/channel-util.h"
#include "io/channel.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
#include "qom/object.h"
#include "system/iothread.h"
#include "tcp-usb.h"

// #define DEBUG_HCD_TCP

#ifdef DEBUG_HCD_TCP
#define DPRINTF(fmt, ...)                                \
    do {                                                 \
        fprintf(stderr, "hcd-tcp: " fmt, ##__VA_ARGS__); \
    } while (0)
#else
#define DPRINTF(fmt, ...) \
    do {                  \
    } while (0)
#endif

static void usb_tcp_host_closed(USBTCPHostState *s)
{
    DPRINTF("%s\n", __func__);
    if (s->ioc != NULL) {
        qio_channel_shutdown(s->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        qio_channel_close(s->ioc, NULL);
        object_unref(OBJECT(s->ioc));
        s->ioc = NULL;
    }
    s->closed = true;
    migrate_del_blocker(&s->migration_blocker);
}

static ssize_t tcp_usb_read(QIOChannel *ioc, void *buf, size_t len)
{
    struct iovec iov = { .iov_base = buf, .iov_len = len };
    bool iolock = bql_locked();
    bool iothread = qemu_in_iothread();
    ssize_t ret = -1;
    Error *err = NULL;

    /*
     * Dont use in IOThread out of co-routine context as
     * it will block IOThread.
     */
    g_assert_true(qemu_in_coroutine() || !iothread);

    if (iolock && !iothread && !qemu_in_coroutine()) {
        bql_unlock();
    }

    ret = qio_channel_readv_full_all_eof(ioc, &iov, 1, NULL, 0, 0, &err);

    if (iolock && !iothread && !qemu_in_coroutine()) {
        bql_lock();
    }

    if (err) {
        error_report_err(err);
    }
    return (ret <= 0) ? ret : iov.iov_len;
}

static bool tcp_usb_write(QIOChannel *ioc, void *buf, ssize_t len)
{
    struct iovec iov = { .iov_base = buf, .iov_len = len };
    bool iolock = bql_locked();
    bool iothread = qemu_in_iothread();
    bool ret = false;
    Error *err = NULL;

    /*
     * Dont use in IOThread out of co-routine context as
     * it will block IOThread.
     */
    g_assert_true(qemu_in_coroutine() || !iothread);

    if (iolock && !iothread && !qemu_in_coroutine()) {
        bql_unlock();
    }

    if (!qio_channel_writev_full_all(ioc, &iov, 1, NULL, 0, 0, &err)) {
        ret = true;
    }

    if (iolock && !iothread && !qemu_in_coroutine()) {
        bql_lock();
    }

    if (err) {
        error_report_err(err);
    }
    return ret;
}

static USBPort *usb_tcp_host_find_active_port(USBTCPHostState *s)
{
    for (int i = 0; i < G_N_ELEMENTS(s->ports) - 1; i++) {
        if (s->ports[i].dev->attached) {
            return &s->ports[i];
        }
    }
    return &s->ports[0];
}

static void coroutine_fn usb_tcp_host_respond_packet_co(void *opaque)
{
    USBTCPPacket *pkt = opaque;
    USBTCPHostState *s = pkt->s;
    USBPacket *p = &pkt->p;
    tcp_usb_header_t hdr = { 0 };
    tcp_usb_response_header resp = { 0 };
    g_autofree void *buffer = NULL;
    USBPort *port = usb_tcp_host_find_active_port(s);

    WITH_QEMU_LOCK_GUARD(&s->write_mutex)
    {
        if (!s->closed) {
            hdr.type = TCP_USB_RESPONSE;
            resp.addr = port->dev->addr;
            resp.pid = p->pid;
            resp.ep = p->ep->nr;
            resp.id = p->id;
            resp.status = p->status;
            resp.length = p->iov.size;

            if (resp.length > p->actual_length) {
                resp.length = p->actual_length;
            }

            if (p->pid == USB_TOKEN_IN && p->status != USB_RET_ASYNC) {
                buffer = g_malloc(resp.length);
                iov_to_buf(p->iov.iov, p->iov.niov, 0, buffer, resp.length);
            }

            if (!tcp_usb_write(s->ioc, &hdr, sizeof(hdr))) {
                usb_tcp_host_closed(s);
                return;
            }

            if (!tcp_usb_write(s->ioc, &resp, sizeof(resp))) {
                usb_tcp_host_closed(s);
                return;
            }

            if (buffer) {
                if (!tcp_usb_write(s->ioc, buffer, resp.length)) {
                    usb_tcp_host_closed(s);
                    return;
                }
            }
        }
    }

    if (!usb_packet_is_inflight(p)) {
        if (pkt->buffer) {
            g_free(pkt->buffer);
        }
        usb_packet_cleanup(p);
        g_free(pkt);
    }
}

static void usb_tcp_host_respond_packet(USBTCPHostState *s, USBTCPPacket *pkt)
{
    Coroutine *co = NULL;
    co = qemu_coroutine_create(usb_tcp_host_respond_packet_co, pkt);
    qemu_coroutine_enter(co);
}

static void coroutine_fn usb_tcp_host_msg_loop_co(void *opaque)
{
    USBTCPHostState *s;
    USBPort *port;
    QIOChannel *ioc;
    tcp_usb_header_t hdr;

    s = opaque;
    port = usb_tcp_host_find_active_port(s);
    ioc = s->ioc;

    for (;;) {
        if (unlikely((tcp_usb_read(ioc, &hdr, sizeof(hdr)) != sizeof(hdr)))) {
            usb_tcp_host_closed(s);
            return;
        }

        switch (hdr.type) {
        case TCP_USB_REQUEST: {
            tcp_usb_request_header pkt_hdr = { 0 };
            g_autofree void *buffer = NULL;
            g_autofree USBTCPPacket *pkt =
                (USBTCPPacket *)g_malloc0(sizeof(USBTCPPacket));
            USBEndpoint *ep = NULL;

            if (unlikely(tcp_usb_read(ioc, &pkt_hdr, sizeof(pkt_hdr)) !=
                         sizeof(pkt_hdr))) {
                usb_tcp_host_closed(s);
                return;
            }

            DPRINTF("%s: TCP_USB_REQUEST pid: 0x%x ep: %d id: 0x%" PRIx64 "\n",
                    __func__, pkt_hdr.pid, pkt_hdr.ep, pkt_hdr.id);
            ep = usb_ep_get(port->dev, pkt_hdr.pid, pkt_hdr.ep);
            if (ep == NULL) {
                fprintf(stderr, "%s: TCP_USB_REQUEST unknown EP\n", __func__);
                usb_tcp_host_closed(s);
                return;
            }

            usb_packet_init(&pkt->p);
            usb_packet_setup(&pkt->p, pkt_hdr.pid, ep, pkt_hdr.stream,
                             pkt_hdr.id, pkt_hdr.short_not_ok, pkt_hdr.int_req);

            if (pkt_hdr.length > 0) {
                buffer = g_malloc0(pkt_hdr.length);

                if (pkt_hdr.pid != USB_TOKEN_IN) {
                    if (unlikely(tcp_usb_read(s->ioc, buffer, pkt_hdr.length) !=
                                 pkt_hdr.length)) {
                        usb_tcp_host_closed(s);
                        usb_packet_cleanup(&pkt->p);
                        return;
                    }
                }

                usb_packet_addbuf(&pkt->p, buffer, pkt_hdr.length);
                pkt->buffer = buffer;
                g_steal_pointer(&buffer);
            }

            pkt->dev = ep->dev;
            pkt->s = s;
            pkt->addr = pkt_hdr.addr;
            g_assert_true(bql_locked());

            usb_handle_packet(pkt->dev, &pkt->p);
            usb_tcp_host_respond_packet(s, pkt);
            g_steal_pointer(&pkt);
            break;
        }
        case TCP_USB_RESPONSE:
            fprintf(stderr, "%s: unexpected TCP_USB_RESPONSE\n", __func__);
            usb_tcp_host_closed(s);
            return;
        case TCP_USB_CANCEL: {
            tcp_usb_cancel_header pkt_hdr = { 0 };
            USBTCPPacket *pkt = NULL;
            USBPacket *p = NULL;

            if (unlikely(tcp_usb_read(ioc, &pkt_hdr, sizeof(pkt_hdr)) !=
                         sizeof(pkt_hdr))) {
                usb_tcp_host_closed(s);
                return;
            }

            DPRINTF("%s: TCP_USB_CANCEL pid: 0x%x ep: %d\n", __func__,
                    pkt_hdr.pid, pkt_hdr.ep);

            g_assert_true(bql_locked());
            p = usb_ep_find_packet_by_id(port->dev, pkt_hdr.pid, pkt_hdr.ep,
                                         pkt_hdr.id);
            if (p) {
                pkt = container_of(p, USBTCPPacket, p);
                usb_cancel_packet(&pkt->p);
                DPRINTF("%s: TCP_USB_CANCEL: packet"
                        " pid: 0x%x ep: %d id: 0x%" PRIx64 " len: 0x%x\n",
                        __func__, pkt_hdr.pid, pkt_hdr.ep, pkt_hdr.id,
                        p->actual_length);
                usb_tcp_host_respond_packet(s, pkt);
            } else {
                warn_report("%s: TCP_USB_CANCEL: packet"
                            " pid: 0x%x ep: %d id: 0x%" PRIx64 " not found",
                            __func__, pkt_hdr.pid, pkt_hdr.ep, pkt_hdr.id);
            }
            break;
        }
        case TCP_USB_RESET:
            DPRINTF("%s: TCP_USB_RESET\n", __func__);
            g_assert_true(bql_locked());
            usb_device_reset(port->dev);
            break;
            ;
        default:
            g_assert_not_reached();
            break;
        }
    }

    return;
}

#ifdef WIN32
static int usb_tcp_host_connect_unix(USBTCPHostState *s, Error **errp)
{
    error_setg(errp, "UNIX sockets are not supported on Windows");
    return -1;
}
#else
static int usb_tcp_host_connect_unix(USBTCPHostState *s, Error **errp)
{
    struct sockaddr_un addr = { 0 };
    int sock;

    if (s->conn_addr == NULL) {
        s->conn_addr = g_strdup(USB_TCP_REMOTE_UNIX_DEFAULT);
        warn_report("No socket path specified, using default (`%s`).",
                    USB_TCP_REMOTE_UNIX_DEFAULT);
    }

    addr.sun_family = AF_UNIX;
    if (strlen(s->conn_addr) >= sizeof(addr.sun_path)) {
        error_setg(errp, "Socket path too long: %s", s->conn_addr);
        return -1;
    }
    strncpy(addr.sun_path, s->conn_addr, sizeof(addr.sun_path));

    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        error_setg_errno(errp, errno, "Cannot open socket");
        return -1;
    }

    if (connect(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        error_setg_errno(errp, errno, "Cannot connect to server");
        close(sock);
        return -1;
    }

    return sock;
}
#endif

static int usb_tcp_host_connect_ipv4(USBTCPHostState *s, Error **errp)
{
    struct sockaddr_in addr = { 0 };
    int ret;
    int sock;

    if (s->conn_port == 0) {
        error_setg(errp, "Port must be specified.");
        return -1;
    }

    if (s->conn_addr == NULL) {
        error_setg(errp, "Address must be specified");
        return -1;
    }

    addr.sin_family = AF_INET;
    ret = inet_pton(AF_INET, s->conn_addr, &addr.sin_addr.s_addr);
    if (ret == 0) {
        error_setg(errp, "Invalid IPv4 address: %s", s->conn_addr);
        return -1;
    } else if (ret < 0) {
        error_setg_errno(errp, errno, "inet_pton failed");
        return -1;
    }
    addr.sin_port = htons(s->conn_port);

    sock = qemu_socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        error_setg_errno(errp, errno, "Cannot open socket");
        return -1;
    }

    if (socket_set_nodelay(sock) < 0) {
        warn_report("Failed to set nodelay for socket: %s", strerror(errno));
    }

    if (connect(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        error_setg_errno(errp, errno, "Cannot connect to server");
        close(sock);
        return -1;
    }

    return sock;
}

static int usb_tcp_host_connect_ipv6(USBTCPHostState *s, Error **errp)
{
    struct sockaddr_in6 addr = { 0 };
    int ret;
    int sock;

    if (s->conn_port == 0) {
        error_setg(errp, "Port must be specified.");
        return -1;
    }

    if (s->conn_addr == NULL) {
        error_setg(errp, "Address must be specified");
        return -1;
    }

    addr.sin6_family = AF_INET6;
    ret = inet_pton(AF_INET6, s->conn_addr, &addr.sin6_addr);
    if (ret == 0) {
        error_setg(errp, "Invalid IPv6 address: %s", s->conn_addr);
        return -1;
    } else if (ret < 0) {
        error_setg_errno(errp, errno, "inet_pton failed");
        return -1;
    }
    addr.sin6_port = htons(s->conn_port);

    sock = qemu_socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        error_setg_errno(errp, errno, "Cannot open socket");
        return -1;
    }

    if (socket_set_nodelay(sock) < 0) {
        warn_report("Failed to set nodelay for socket: %s", strerror(errno));
    }

    if (connect(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        error_setg_errno(errp, errno, "Cannot connect to server");
        close(sock);
        return -1;
    }

    return sock;
}

static void usb_tcp_host_attach(USBPort *port)
{
    USBTCPHostState *s;
    int sock;
    Coroutine *co;
    QIOChannel *ioc;
    Error *err;

    s = port->opaque;
    err = NULL;

    if (port->index >= G_N_ELEMENTS(s->ports) - 1) {
        error_report("%s: attached to unused port\n", __func__);
        return;
    }

    if (usb_tcp_host_find_active_port(s) != port) {
        error_report("%s: Attaching to 2 proxy ports at the same time. "
                     "This port might not be able to receive packets.\n",
                     __func__);
        return;
    }

    if (port->dev == NULL || !port->dev->attached) {
        return;
    }

    switch (s->conn_type) {
    case TCP_REMOTE_CONN_TYPE_UNIX:
        sock = usb_tcp_host_connect_unix(s, &err);
        break;
    case TCP_REMOTE_CONN_TYPE_IPV4:
        sock = usb_tcp_host_connect_ipv4(s, &err);
        break;
    case TCP_REMOTE_CONN_TYPE_IPV6:
        sock = usb_tcp_host_connect_ipv6(s, &err);
        break;
    default:
        g_assert_not_reached();
    }

    if (sock == -1) {
        error_report_err(err);
        return;
    }

    ioc = qio_channel_new_fd(sock, &err);
    if (ioc == NULL) {
        error_report_err(err);
        close(sock);
        return;
    }

    object_ref(ioc);
    qio_channel_set_blocking(ioc, false, NULL);
    s->closed = false;
    s->ioc = ioc;

    migrate_add_blocker(&s->migration_blocker, NULL);
    co = qemu_coroutine_create(usb_tcp_host_msg_loop_co, s);
    qemu_coroutine_enter(co);
}

static void usb_tcp_host_detach(USBPort *port)
{
    USBTCPHostState *s;

    s = port->opaque;

    usb_tcp_host_closed(s);
}

static void usb_tcp_host_async_packet_complete(USBPort *port, USBPacket *p)
{
    USBTCPHostState *s;

    s = port->opaque;

    usb_tcp_host_respond_packet(s, container_of(p, USBTCPPacket, p));
}

static USBBusOps usb_tcp_bus_ops = {};

static USBPortOps usb_tcp_host_port_ops = {
    .attach = usb_tcp_host_attach,
    .detach = usb_tcp_host_detach,
    .child_detach = NULL,
    .wakeup = NULL,
    .complete = usb_tcp_host_async_packet_complete,
};

static void usb_tcp_host_realize(DeviceState *dev, Error **errp)
{
    USBTCPHostState *s;
    int i;

    s = USB_TCP_HOST(dev);

    usb_bus_new(&s->bus, sizeof(s->bus), &usb_tcp_bus_ops, dev);
    for (i = 0; i < G_N_ELEMENTS(s->ports); i++) {
        usb_register_port(&s->bus, &s->ports[i], s, i, &usb_tcp_host_port_ops,
                          USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL |
                              USB_SPEED_MASK_HIGH);
    }

    s->closed = true;
    qemu_co_mutex_init(&s->write_mutex);
}

static void usb_tcp_host_unrealize(DeviceState *dev)
{
    USBTCPHostState *s = USB_TCP_HOST(dev);

    if (s->ioc) {
        qio_channel_shutdown(s->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        qio_channel_close(s->ioc, NULL);
        s->ioc = NULL;
    }

    s->closed = true;
    s->stopped = true;
}

static void usb_tcp_host_init(Object *obj)
{
    USBTCPHostState *s = USB_TCP_HOST(obj);
    s->closed = true;
    error_setg(&s->migration_blocker,
               "%s does not support migration while connected",
               TYPE_USB_TCP_HOST);
}

static const Property usb_tcp_host_props[] = {
    DEFINE_PROP_USB_TCP_REMOTE_CONN_TYPE("conn-type", USBTCPHostState,
                                         conn_type, TCP_REMOTE_CONN_TYPE_UNIX),
    DEFINE_PROP_STRING("conn-addr", USBTCPHostState, conn_addr),
    DEFINE_PROP_UINT16("conn-port", USBTCPHostState, conn_port, 0),
};

static void usb_tcp_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = usb_tcp_host_realize;
    dc->unrealize = usb_tcp_host_unrealize;
    dc->desc = "QEMU USB Passthrough Host Controller";
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    device_class_set_props(dc, usb_tcp_host_props);
}

static const TypeInfo usb_tcp_host_type_info = {
    .name = TYPE_USB_TCP_HOST,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(USBTCPHostState),
    .class_init = usb_tcp_host_class_init,
    .instance_init = usb_tcp_host_init,
};

static void usb_tcp_host_register_types(void)
{
    type_register_static(&usb_tcp_host_type_info);
}

type_init(usb_tcp_host_register_types)
