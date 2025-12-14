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

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qom/object.h"
#include "dev-tcp-remote.h"
#include "tcp-usb.h"
#include "trace.h"

#if 0
#define DPRINTF(fmt, ...)                                       \
    do {                                                        \
        fprintf(stderr, "dev-tcp-remote: " fmt, ##__VA_ARGS__); \
    } while (0)
#else
#define DPRINTF(fmt, ...) \
    do {                  \
    } while (0)
#endif

static USBTCPInflightPacket *
usb_tcp_remote_find_inflight_packet(USBTCPRemoteState *s, int pid, uint8_t ep,
                                    uint64_t id)
{
    USBTCPInflightPacket *p;

    QEMU_LOCK_GUARD(&s->queue_mutex);

    QTAILQ_FOREACH (p, &s->queue, queue) {
        if (p->p->pid == pid && p->p->ep->nr == ep && p->p->id == id) {
            return p;
        }
    }

    return NULL;
}

static void usb_tcp_remote_clean_inflight_queue(USBTCPRemoteState *s)
{
    USBTCPInflightPacket *p;

    QEMU_LOCK_GUARD(&s->queue_mutex);

    QTAILQ_FOREACH (p, &s->queue, queue) {
        p->p->status = USB_RET_STALL;
        qatomic_set(&p->handled, 1);
        /* Will be cleaned by usb_tcp_remote_handle_packet */
    }
}

static void usb_tcp_remote_clean_completed_queue(USBTCPRemoteState *s)
{
    USBTCPCompletedPacket *p;
    USBDevice *dev = USB_DEVICE(s);

    QEMU_LOCK_GUARD(&s->completed_queue_mutex);

    while (!QTAILQ_EMPTY(&s->completed_queue)) {
        p = QTAILQ_FIRST(&s->completed_queue);
        QTAILQ_REMOVE(&s->completed_queue, p, queue);
        p->p->status = USB_RET_STALL;
        if (p->p->status == USB_RET_REMOVE_FROM_QUEUE) {
            dev->port->ops->complete(dev->port, p->p);
        } else {
            usb_packet_complete(USB_DEVICE(s), p->p);
        }
        g_free(p);
    }
}

static void usb_tcp_remote_cleanup(void *opaque)
{
    USBTCPRemoteState *s = opaque;

    if (s->fd == -1) {
        return;
    }

    close(s->fd);

    s->fd = -1;
    s->closed = true;
    s->addr = 0;

    usb_tcp_remote_clean_completed_queue(s);

    if (USB_DEVICE(s)->attached) {
        usb_device_detach(USB_DEVICE(s));
    }

    qemu_cond_broadcast(&s->cond);
    migrate_del_blocker(&s->migration_blocker);
}

static void usb_tcp_remote_update_addr_bh(void *opaque)
{
    USBTCPRemoteState *s = opaque;
    USBDevice *dev = USB_DEVICE(s);
    dev->addr = s->addr;
    trace_usb_set_addr(dev->addr);
}

static void usb_tcp_remote_completed_bh(void *opaque)
{
    USBTCPRemoteState *s = opaque;
    USBDevice *dev = USB_DEVICE(s);

    USBTCPCompletedPacket *p;

    QEMU_LOCK_GUARD(&s->completed_queue_mutex);

    while (!QTAILQ_EMPTY(&s->completed_queue)) {
        p = QTAILQ_FIRST(&s->completed_queue);
        QTAILQ_REMOVE(&s->completed_queue, p, queue);

        qemu_mutex_unlock(&s->completed_queue_mutex);
        if (s->addr != dev->addr && p->p->ep->nr == 0 &&
            p->p->pid == USB_TOKEN_IN && p->p->status == USB_RET_SUCCESS) {
            /*
             * EHCI will append the completed packet to a queue
             * and then schedule a BH
             * BH scheduling is FIFO
             * we want addr to be update after the IN status completed
             */
            qemu_bh_schedule(s->addr_bh);
        }
        if (usb_packet_is_inflight(p->p)) {
            if (p->p->status == USB_RET_REMOVE_FROM_QUEUE) {
                dev->port->ops->complete(dev->port, p->p);
            } else {
                usb_packet_complete(USB_DEVICE(s), p->p);
            }
        }
        g_free(p);
        qemu_mutex_lock(&s->completed_queue_mutex);
    }
}

static void usb_tcp_remote_closed(USBTCPRemoteState *s)
{
    if (s->fd == -1) {
        return;
    }

    s->closed = true;
    smp_wmb();

    DPRINTF("%s\n", __func__);
    /* Cleanup inflights, otherwise mainloop is stuck */
    usb_tcp_remote_clean_inflight_queue(s);
    qemu_bh_schedule(s->cleanup_bh);
}

static int usb_tcp_remote_read(USBTCPRemoteState *s, void *buffer,
                               unsigned int length)
{
    int ret = 0;
    int n = 0;
    bool locked = bql_locked();
    if (locked) {
        bql_unlock();
    }

    while (n < length) {
        ret = recv(s->fd, buffer + n, length - n, 0);
        if (ret <= 0) {
            if (locked) {
                bql_lock();
            }
            usb_tcp_remote_closed(s);
            return -errno;
        }

        n += ret;
    }

    if (locked) {
        bql_lock();
    }

    return n;
}

static int usb_tcp_remote_write(USBTCPRemoteState *s, void *buffer,
                                unsigned int length)
{
    int ret = 0;
    int n = 0;

    while (n < length) {
        ret = send(s->fd, (char *)buffer + n, length - n, 0);
        if (ret <= 0) {
            usb_tcp_remote_closed(s);
            return -errno;
        }

        n += ret;
    }

    return n;
}

static bool usb_tcp_remote_read_one(USBTCPRemoteState *s)
{
    tcp_usb_header_t hdr = { 0 };

    if (usb_tcp_remote_read(s, &hdr, sizeof(hdr)) < sizeof(hdr)) {
        return false;
    }

    switch (hdr.type) {
    case TCP_USB_RESPONSE: {
        tcp_usb_response_header rhdr = { 0 };
        USBPacket *p = NULL;
        USBTCPInflightPacket *pkt = NULL;
        bool cancelled = false;

        if (usb_tcp_remote_read(s, &rhdr, sizeof(rhdr)) < sizeof(rhdr)) {
            return false;
        }

        smp_rmb();
        pkt =
            usb_tcp_remote_find_inflight_packet(s, rhdr.pid, rhdr.ep, rhdr.id);
        if (pkt == NULL) {
            p = usb_ep_find_packet_by_id(USB_DEVICE(s), rhdr.pid, rhdr.ep,
                                         rhdr.id);
        } else {
            p = pkt->p;
        }
        DPRINTF("%s: TCP_USB_RESPONSE "
                "Received packet pid: 0x%x ep: %d id: 0x%" PRIx64
                " status: %d\n",
                __func__, rhdr.pid, rhdr.ep, rhdr.id, rhdr.status);

        if (p == NULL) {
            warn_report("%s: TCP_USB_RESPONSE "
                        "Invalid packet pid: 0x%x ep: %d id: 0x%" PRIx64 "\n",
                        __func__, rhdr.pid, rhdr.ep, rhdr.id);
            //__builtin_dump_struct(&rhdr, &printf);
            /* likely canceled */
            /* When an EP is aborted, all of its queued packets are removed */
        }

        if (rhdr.length > 0 && rhdr.status != USB_RET_ASYNC) {
            g_autofree void *buffer = g_malloc(rhdr.length);
            if (rhdr.pid == USB_TOKEN_IN) {
                if (usb_tcp_remote_read(s, buffer, rhdr.length) < rhdr.length) {
                    return false;
                }
                if (p) {
                    usb_packet_copy(p, buffer, rhdr.length);
                }
            } else if (p) {
                p->actual_length += rhdr.length;
            }
        }

        if (!p) {
            return true;
        }

        p->status = rhdr.status;
        if (p->state == USB_PACKET_ASYNC) {
            if (p->status == USB_RET_NAK || p->status == USB_RET_ASYNC) {
                fprintf(stderr,
                        "%s: TCP_USB_RESPONSE "
                        "USB_RET_NAK|ASYNC an ASYNC packet",
                        __func__);
                usb_tcp_remote_closed(s);
                return false;
            }
        }
        if (p->state == USB_PACKET_QUEUED) {
            if (p->status == USB_RET_NAK) {
                p->status = USB_RET_IOERROR;
            }
        }
        if (p->state == USB_PACKET_CANCELED) {
            cancelled = true;
        }
        if (((p->status != USB_RET_SUCCESS && p->status != USB_RET_ASYNC &&
              p->status != USB_RET_NAK) ||
             cancelled) &&
            p->ep->nr == 0 && p->pid == USB_TOKEN_IN) {
            s->addr = USB_DEVICE(s)->addr;
        }
        if (pkt) {
            pkt->addr = rhdr.addr;
            qatomic_set(&pkt->handled, 1);
        } else if (p->status != USB_RET_ASYNC && !cancelled) {
            USBTCPCompletedPacket *c = g_malloc0(sizeof(USBTCPCompletedPacket));
            c->p = p;
            c->addr = rhdr.addr;
            smp_wmb();
            WITH_QEMU_LOCK_GUARD(&s->completed_queue_mutex)
            {
                QTAILQ_INSERT_TAIL(&s->completed_queue, c, queue);
                qemu_cond_broadcast(&s->completed_queue_cond);
            }
            smp_wmb();
            qemu_bh_schedule(s->completed_bh);
        }
        return true;
    }

    case TCP_USB_REQUEST:
    case TCP_USB_RESET:
    default:
        // "Invalid header type: 0x0" can happen upon closing the connection
        DPRINTF("%s: Invalid header type: 0x%x\n", __func__, hdr.type);
        usb_tcp_remote_closed(s);
        return false;
    }
}

static void *usb_tcp_remote_read_thread(void *opaque)
{
    USBTCPRemoteState *s = opaque;

    bql_lock();
    while (usb_tcp_remote_read_one(s) && !s->closed) {
        continue;
    }
    bql_unlock();

    return NULL;
}

static void *usb_tcp_remote_thread(void *arg)
{
    USBTCPRemoteState *s = arg;

    while (!s->stopped) {
        // thanks to Visual for noticing that
        qemu_mutex_lock(&s->mutex);

        if (s->closed) {
            DPRINTF("%s: waiting on accept...\n", __func__);

            while ((s->fd = accept(s->socket, NULL, NULL)) < 0 && !s->stopped) {
                DPRINTF("%s: accept failed: %s.\n", __func__, strerror(errno));
            }

            if (s->fd < 0) {
                continue;
            }

            migrate_add_blocker(&s->migration_blocker, NULL);

            s->closed = false;

            qemu_cond_broadcast(&s->cond);

            DPRINTF("%s: USB device accepted!\n", __func__);

            bql_lock();
            usb_device_attach(USB_DEVICE(s), &error_abort);
            bql_unlock();
            qemu_thread_create(&s->read_thread, TYPE_USB_TCP_REMOTE ".read",
                               usb_tcp_remote_read_thread, s,
                               QEMU_THREAD_JOINABLE);
        }

        while (!s->closed) {
            qemu_cond_wait(&s->cond, &s->mutex);
        }

        qemu_mutex_unlock(&s->mutex);
    }

    return NULL;
}

#ifdef WIN32
static void usb_tcp_remote_bind_unix(USBTCPRemoteState *s, Error **errp)
{
    error_setg(errp, "UNIX sockets are not supported on Windows");
}
#else
static void usb_tcp_remote_bind_unix(USBTCPRemoteState *s, Error **errp)
{
    struct sockaddr_un addr = { 0 };
    struct stat addr_stat = { 0 };

    if (s->conn_addr == NULL) {
        s->conn_addr = g_strdup(USB_TCP_REMOTE_UNIX_DEFAULT);
        warn_report("No socket path specified, using default (`%s`).",
                    USB_TCP_REMOTE_UNIX_DEFAULT);
    }

    if (lstat(s->conn_addr, &addr_stat) == 0) {
        if (!S_ISSOCK(addr_stat.st_mode)) {
            error_setg(errp, "Existing file at `%s` is not a socket",
                       s->conn_addr);
            return;
        }
    }

    if (unlink(s->conn_addr) < 0 && errno != ENOENT) {
        error_setg_errno(errp, errno, "unlink('%s') failed", s->conn_addr);
        return;
    }

    s->socket = qemu_socket(AF_UNIX, SOCK_STREAM, 0);
    if (s->socket < 0) {
        error_setg_errno(errp, errno, "Cannot open socket");
        return;
    }

    addr.sun_family = AF_UNIX;
    if (strlen(s->conn_addr) >= sizeof(addr.sun_path)) {
        error_setg(errp, "Socket path too long: %s", s->conn_addr);
        return;
    }
    strncpy(addr.sun_path, s->conn_addr, sizeof(addr.sun_path));

    if (bind(s->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        error_setg_errno(errp, errno, "Cannot bind socket");
        return;
    }

    if (chmod(s->conn_addr, 0666) < 0) {
        warn_report("chmod('%s') failed: %s", s->conn_addr, strerror(errno));
    }
}
#endif

static void usb_tcp_remote_bind_ipv4(USBTCPRemoteState *s, Error **errp)
{
    struct sockaddr_in addr = { 0 };
    int ret;

    if (s->conn_port == 0) {
        error_setg(errp, "Port must be specified.");
        return;
    }

    addr.sin_family = AF_INET;
    if (s->conn_addr == NULL) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        ret = inet_pton(AF_INET, s->conn_addr, &addr.sin_addr.s_addr);
        if (ret == 0) {
            error_setg(errp, "Invalid IPv4 address: %s", s->conn_addr);
            return;
        } else if (ret < 0) {
            error_setg_errno(errp, errno, "inet_pton failed");
            return;
        }
    }
    addr.sin_port = htons(s->conn_port);

    s->socket = qemu_socket(PF_INET, SOCK_STREAM, 0);
    if (s->socket < 0) {
        error_setg_errno(errp, errno, "Cannot open socket");
        return;
    }
    if (socket_set_nodelay(s->socket) < 0) {
        warn_report("Failed to set nodelay for socket: %s", strerror(errno));
    }
    if (bind(s->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->socket);
        error_setg_errno(errp, errno, "Cannot bind socket");
        return;
    }
}

static void usb_tcp_remote_bind_ipv6(USBTCPRemoteState *s, Error **errp)
{
    struct sockaddr_in6 addr = { 0 };
    int ret;

    if (s->conn_port == 0) {
        error_setg(errp, "Port must be specified.");
        return;
    }

    addr.sin6_family = AF_INET6;
    if (s->conn_addr == NULL) {
        addr.sin6_addr = in6addr_any;
    } else {
        ret = inet_pton(AF_INET6, s->conn_addr, &addr.sin6_addr);
        if (ret == 0) {
            error_setg(errp, "Invalid IPv6 address: %s", s->conn_addr);
            return;
        } else if (ret < 0) {
            error_setg_errno(errp, errno, "inet_pton failed");
            return;
        }
    }
    addr.sin6_port = htons(s->conn_port);

    s->socket = qemu_socket(PF_INET, SOCK_STREAM, 0);
    if (s->socket < 0) {
        error_setg_errno(errp, errno, "Cannot open socket");
        return;
    }
    if (socket_set_nodelay(s->socket) < 0) {
        warn_report("Failed to set nodelay for socket: %s", strerror(errno));
    }
    if (bind(s->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->socket);
        error_setg_errno(errp, errno, "Cannot bind socket");
        return;
    }
}

static void usb_tcp_remote_realize(USBDevice *dev, Error **errp)
{
    USBTCPRemoteState *s = USB_TCP_REMOTE(dev);

    dev->speed = USB_SPEED_HIGH;
    dev->speedmask = USB_SPEED_MASK_HIGH;
    dev->flags |= (1 << USB_DEV_FLAG_IS_HOST);
    dev->auto_attach = 0;

    qemu_cond_init(&s->cond);
    qemu_mutex_init(&s->mutex);
    qemu_mutex_init(&s->request_mutex);

    qemu_mutex_init(&s->queue_mutex);
    QTAILQ_INIT(&s->queue);

    qemu_mutex_init(&s->completed_queue_mutex);
    qemu_cond_init(&s->completed_queue_cond);
    QTAILQ_INIT(&s->completed_queue);

    s->completed_bh = qemu_bh_new(usb_tcp_remote_completed_bh, s);
    s->addr_bh = qemu_bh_new(usb_tcp_remote_update_addr_bh, s);
    s->cleanup_bh = qemu_bh_new(usb_tcp_remote_cleanup, s);

    s->socket = -1;
    s->fd = -1;
    s->closed = true;

    switch (s->conn_type) {
    case TCP_REMOTE_CONN_TYPE_UNIX:
        usb_tcp_remote_bind_unix(s, errp);
        break;
    case TCP_REMOTE_CONN_TYPE_IPV4:
        usb_tcp_remote_bind_ipv4(s, errp);
        break;
    case TCP_REMOTE_CONN_TYPE_IPV6:
        usb_tcp_remote_bind_ipv6(s, errp);
        break;
    default:
        g_assert_not_reached();
    }

    if (listen(s->socket, 1) < 0) {
        error_setg(errp, "Cannot listen on socket");
        return;
    }

    error_setg(&s->migration_blocker,
               "%s does not support migration "
               "while connected",
               TYPE_USB_TCP_REMOTE);
    qemu_thread_create(&s->thread, TYPE_USB_TCP_REMOTE ".thread",
                       &usb_tcp_remote_thread, s, QEMU_THREAD_JOINABLE);
}

static void usb_tcp_remote_unrealize(USBDevice *dev)
{
    USBTCPRemoteState *s = USB_TCP_REMOTE(dev);

    if (s->socket >= 0) {
        close(s->socket);
        s->socket = -1;
    }

    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }

    s->closed = true;

    qemu_cond_broadcast(&s->cond);

    s->stopped = true;
    usb_tcp_remote_clean_inflight_queue(s);
    usb_tcp_remote_clean_completed_queue(s);
}

static void usb_tcp_remote_handle_reset(USBDevice *dev)
{
    tcp_usb_header_t hdr = { 0 };
    USBTCPRemoteState *s = USB_TCP_REMOTE(dev);

    if (s->closed) {
        return;
    }

    DPRINTF("%s\n", __func__);
    usb_tcp_remote_clean_inflight_queue(s);
    usb_tcp_remote_clean_completed_queue(s);
    s->addr = 0;
    hdr.type = TCP_USB_RESET;

    WITH_QEMU_LOCK_GUARD(&s->request_mutex)
    {
        usb_tcp_remote_write(s, &hdr, sizeof(hdr));
    }
}

static void usb_tcp_remote_cancel_packet(USBDevice *dev, USBPacket *p)
{
    USBTCPRemoteState *s = USB_TCP_REMOTE(dev);
    USBTCPInflightPacket inflightPacket = { 0 };
    tcp_usb_header_t hdr = { 0 };
    tcp_usb_cancel_header pkt = { 0 };
    bool locked = bql_locked();
    int64_t start;

    if (p->combined) {
        usb_combined_packet_cancel(dev, p);
        return;
    }

    if (s->closed) {
        return;
    }

    hdr.type = TCP_USB_CANCEL;
    pkt.addr = s->addr;
    pkt.pid = p->pid;
    pkt.ep = p->ep->nr;
    pkt.id = p->id;

    DPRINTF("%s: pid: 0x%x ep %d id 0x%" PRIx64 "\n", __func__, pkt.pid, pkt.ep,
            pkt.id);

    inflightPacket.p = p;
    inflightPacket.addr = dev->addr;
    qatomic_set(&inflightPacket.handled, 0);

    WITH_QEMU_LOCK_GUARD(&s->queue_mutex)
    {
        QTAILQ_INSERT_TAIL(&s->queue, &inflightPacket, queue);
    }

    WITH_QEMU_LOCK_GUARD(&s->request_mutex)
    {
        usb_tcp_remote_write(s, &hdr, sizeof(hdr));
        usb_tcp_remote_write(s, &pkt, sizeof(pkt));
    }
    /* TODO: wait for status */

    DPRINTF("%s: waiting for response\n", __func__);
    if (locked) {
        bql_unlock();
    }

    start = get_clock_realtime();
    while ((qatomic_read(&inflightPacket.handled) & 1) == 0) {
        if (start + NANOSECONDS_PER_SECOND < get_clock_realtime()) {
            break;
        }
    }

    if (locked) {
        bql_lock();
    }

    WITH_QEMU_LOCK_GUARD(&s->queue_mutex)
    {
        QTAILQ_REMOVE(&s->queue, &inflightPacket, queue);
    }
}

static void usb_tcp_remote_handle_packet(USBDevice *dev, USBPacket *p)
{
    USBTCPRemoteState *s = USB_TCP_REMOTE(dev);
    tcp_usb_header_t hdr = { 0 };
    tcp_usb_request_header pkt = { 0 };
    USBTCPInflightPacket inflightPacket = { 0 };
    g_autofree void *buffer = NULL;
    bool locked = bql_locked();

    if (s->closed) {
        p->status = USB_RET_STALL;
        return;
    }

    hdr.type = TCP_USB_REQUEST;
    pkt.addr = s->addr;
    pkt.pid = p->pid;
    pkt.ep = p->ep->nr;
    pkt.id = p->id;
    pkt.stream = p->stream;
    pkt.short_not_ok = p->short_not_ok;
    pkt.int_req = p->int_req;
    pkt.length = p->iov.size - p->actual_length;

    DPRINTF("%s: pid: 0x%x ep %d id 0x%" PRIx64 " len 0x%x\n", __func__,
            pkt.pid, pkt.ep, pkt.id, pkt.length);

    if (p->pid != USB_TOKEN_IN && pkt.length) {
        buffer = g_malloc0(pkt.length);
        usb_packet_copy(p, buffer, pkt.length);
        p->actual_length -= pkt.length;
        if (p->pid == USB_TOKEN_SETUP && p->ep->nr == 0 && buffer) {
            struct usb_control_packet *setup =
                (struct usb_control_packet *)buffer;
            if (setup->bmRequestType == 0 &&
                setup->bRequest == USB_REQ_SET_ADDRESS) {
                s->addr = setup->wValue;
            }
        }
    }

    inflightPacket.p = p;
    inflightPacket.addr = dev->addr;
    qatomic_set(&inflightPacket.handled, 0);

    WITH_QEMU_LOCK_GUARD(&s->queue_mutex)
    {
        QTAILQ_INSERT_TAIL(&s->queue, &inflightPacket, queue);
    }
    /* Retire the writes so that the read thread can find it */
    smp_wmb();

    WITH_QEMU_LOCK_GUARD(&s->request_mutex)
    {
        if (usb_tcp_remote_write(s, &hdr, sizeof(hdr)) < sizeof(hdr)) {
            p->status = USB_RET_STALL;
            goto out;
        }

        if (usb_tcp_remote_write(s, &pkt, sizeof(pkt)) < sizeof(pkt)) {
            p->status = USB_RET_STALL;
            goto out;
        }

        if (buffer) {
            if (usb_tcp_remote_write(s, buffer, pkt.length) < pkt.length) {
                p->status = USB_RET_STALL;
                goto out;
            }
        }
    }

    if (locked) {
        bql_unlock();
    }

    while ((qatomic_read(&inflightPacket.handled) & 1) == 0) {
    }

    if (locked) {
        bql_lock();
    }

out:
    if (s->addr != dev->addr && p->ep->nr == 0 && p->pid == USB_TOKEN_IN &&
        p->status == USB_RET_SUCCESS) {
        dev->addr = s->addr;
        trace_usb_set_addr(dev->addr);
    }

    WITH_QEMU_LOCK_GUARD(&s->queue_mutex)
    {
        QTAILQ_REMOVE(&s->queue, &inflightPacket, queue);
    }
}

static const Property usb_tcp_remote_dev_props[] = {
    DEFINE_PROP_USB_TCP_REMOTE_CONN_TYPE("conn-type", USBTCPRemoteState,
                                         conn_type, TCP_REMOTE_CONN_TYPE_UNIX),
    DEFINE_PROP_STRING("conn-addr", USBTCPRemoteState, conn_addr),
    DEFINE_PROP_UINT16("conn-port", USBTCPRemoteState, conn_port, 0),
};

static void usb_tcp_remote_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize = usb_tcp_remote_realize;
    uc->unrealize = usb_tcp_remote_unrealize;
    uc->handle_attach = NULL;
    uc->handle_detach = NULL;
    uc->cancel_packet = usb_tcp_remote_cancel_packet;
    uc->handle_reset = usb_tcp_remote_handle_reset;
    uc->handle_control = NULL;
    uc->handle_data = NULL;
    uc->handle_packet = usb_tcp_remote_handle_packet;
    uc->product_desc = "QEMU USB Passthrough Device";

    dc->desc = "QEMU USB Passthrough Device";
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    device_class_set_props(dc, usb_tcp_remote_dev_props);
}

static const TypeInfo usb_tcp_remote_dev_type_info = {
    .name = TYPE_USB_TCP_REMOTE,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBTCPRemoteState),
    .class_init = usb_tcp_remote_dev_class_init,
};

static void usb_tcp_register_types(void)
{
    type_register_static(&usb_tcp_remote_dev_type_info);
}

type_init(usb_tcp_register_types)
