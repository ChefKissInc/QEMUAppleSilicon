/*
 * Apple Smart Input/Output DMA Controller.
 *
 * Copyright (c) 2024-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
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
#include "hw/dma/apple_sio.h"
#include "hw/misc/apple-silicon/a7iop/rtkit.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/queue.h"
#include "system/dma.h"

#if 0
#define SIO_LOG_MSG(ep, msg)                                                \
    do {                                                                    \
        qemu_log_mask(LOG_GUEST_ERROR,                                      \
                      "SIO: message: ep=%X msg=0x" HWADDR_FMT_plx "\n", ep, \
                      msg);                                                 \
    } while (0)
#else
#define SIO_LOG_MSG(ep, msg) \
    do {                     \
    } while (0)
#endif

#define SIO_NUM_EPS (0xDC)

typedef struct {
    uint32_t xfer;
    uint32_t timeout;
    uint32_t fifo;
    uint32_t trigger;
    uint32_t fifo_depth;
    uint32_t field_14;
    uint32_t field_18;
} QEMU_PACKED SIODMAConfig;

typedef struct {
    uint64_t addr;
    uint32_t len;
} QEMU_PACKED SIODMASegment;

typedef struct SIODMAMapRequest {
    SIODMASegment *segments;
    QEMUSGList sgl;
    QEMUIOVector iov;
    uint32_t segment_count;
    uint64_t bytes_accessed;
    uint32_t tag;
    bool mapped;
    QTAILQ_ENTRY(SIODMAMapRequest) next;
} SIODMAMapRequest;

struct AppleSIODMAEndpoint {
    SIODMAConfig config;
    DMADirection direction;
    QemuMutex mutex;
    uint32_t id;
    QTAILQ_HEAD(, SIODMAMapRequest) requests;
};

struct AppleSIOClass {
    /*< private >*/
    AppleRTKitClass base_class;

    /*< public >*/
    DeviceRealize parent_realize;
    ResettablePhases parent_reset;
};

struct AppleSIOState {
    /*< private >*/
    AppleRTKit parent_obj;

    /*< public >*/
    MemoryRegion ascv2_iomem;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    AppleSIODMAEndpoint eps[SIO_NUM_EPS];
    uint32_t protocol_version;
    uint64_t segment_base;
    uint32_t segment_size;
};

typedef enum {
    OP_GET_PARAM = 2,
    OP_SET_PARAM = 3,
    OP_CONFIGURE = 5,
    OP_MAP = 6,
    OP_QUERY = 7,
    OP_STOP = 8,
    OP_ACK = 101,
    OP_GET_PARAM_RESP = 103,
    OP_COMPLETE = 104,
    OP_QUERY_OK = 105,
    // Errors
    OP_SYNC_ERROR = 2,
    OP_SET_PARAM_ERROR = 3,
    OP_ASYNC_ERROR = 102,
} SIOOp;

typedef enum {
    EP_CONTROL = 0,
    EP_PERF = 3,
} SIOEndpoint;

typedef enum {
    PARAM_PROTOCOL_VERSION = 0,
    PARAM_SEGMENT_BASE,
    PARAM_SEGMENT_SIZE,
    PARAM_RESPONSE_BASE = 11,
    PARAM_RESPONSE_SIZE,
    PARAM_PERF_BASE,
    PARAM_PERF_SIZE,
    PARAM_PANIC_BASE,
    PARAM_PANIC_SIZE,
    PARAM_PIO_BASE = 26,
    PARAM_PIO_SIZE,
    PARAM_DEVICES_BASE,
    PARAM_DEVICES_SIZE,
    PARAM_ASCWRAP_TUNABLES_BASE,
    PARAM_ASCWRAP_TUNABLES_SIZE,
    PARAM_MISC_TUNABLES_BASE,
    PARAM_MISC_TUNABLES_SIZE,
    PARAM_SHIMS_BASE,
    PARAM_SHIMS_SIZE,
    PARAM_PS_REGS_BASE,
    PARAM_PS_REGS_SIZE,
    PARAM_FORWARD_IRQS_BASE,
    PARAM_FORWARD_IRQS_SIZE,
} SmartIOParameter;

typedef struct QEMU_PACKED {
    union {
        uint64_t raw;
        struct QEMU_PACKED {
            uint8_t ep;
            uint8_t tag;
            uint8_t op;
            uint8_t param;
            uint32_t data;
        };
    };
} SIOMessage;

static bool apple_sio_map_dma(AppleSIODMAEndpoint *ep, SIODMAMapRequest *req)
{
    if (req->mapped) {
        return true;
    }

    qemu_iovec_init(&req->iov, req->segment_count);

    for (int i = 0; i < req->segment_count; ++i) {
        dma_addr_t base = req->sgl.sg[i].base;
        dma_addr_t len = req->sgl.sg[i].len;

        while (len) {
            dma_addr_t xlen = len;
            void *mem = dma_memory_map(req->sgl.as, base, &xlen, ep->direction,
                                       MEMTXATTRS_UNSPECIFIED);
            if (mem == NULL) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: dma_memory_map failed; req->tag=%d, "
                              "base=0x%" HWADDR_PRIX ", len=0x%" HWADDR_PRIX
                              ", ep->direction=%d\n",
                              __func__, req->tag, base, len, ep->direction);
                qemu_iovec_destroy(&req->iov);
                return false;
            }

            if (xlen > len) {
                xlen = len;
            }
            qemu_iovec_add(&req->iov, mem, xlen);
            len -= xlen;
            base += xlen;
        }
    }

    req->mapped = true;
    return true;
}

static void apple_sio_destroy_req(AppleSIODMAEndpoint *ep,
                                  SIODMAMapRequest *req)
{
    int i;
    uint32_t unmap_length;
    uint64_t access_len;

    if (req->mapped) {
        unmap_length = req->bytes_accessed;
        for (i = 0; i < req->iov.niov; ++i) {
            access_len = req->iov.iov[i].iov_len;
            if (access_len > unmap_length) {
                access_len = unmap_length;
            }

            dma_memory_unmap(req->sgl.as, req->iov.iov[i].iov_base,
                             req->iov.iov[i].iov_len, ep->direction,
                             access_len);
            unmap_length -= access_len;
        }
    }

    qemu_iovec_destroy(&req->iov);
    qemu_sglist_destroy(&req->sgl);
    g_free(req->segments);
    QTAILQ_REMOVE(&ep->requests, req, next);
    g_free(req);
}

static void apple_sio_stop(AppleSIODMAEndpoint *ep)
{
    SIODMAMapRequest *req;
    SIODMAMapRequest *next;

    QTAILQ_FOREACH_SAFE (req, &ep->requests, next, next) {
        apple_sio_destroy_req(ep, req);
    }
}

static void apple_sio_dma_writeback(AppleSIOState *s, AppleSIODMAEndpoint *ep,
                                    SIODMAMapRequest *req)
{
    AppleRTKit *rtk;
    SIOMessage m = { 0 };

    rtk = APPLE_RTKIT(s);

    m.op = OP_COMPLETE;
    m.ep = ep->id;
    m.param = (1 << 7);
    m.tag = req->tag;
    m.data = req->bytes_accessed;

    apple_sio_destroy_req(ep, req);

    apple_rtkit_send_user_msg(rtk, EP_CONTROL, m.raw);
}

uint64_t apple_sio_dma_read(AppleSIODMAEndpoint *ep, void *buffer, uint64_t len)
{
    AppleSIOState *s;
    SIODMAMapRequest *req;
    uint64_t iovec_len;
    uint64_t actual_len = 0;

    QEMU_LOCK_GUARD(&ep->mutex);

    g_assert_cmpuint(ep->direction, ==, DMA_DIRECTION_TO_DEVICE);

    s = container_of(ep, AppleSIOState, eps[ep->id]);

    while (len > actual_len) {
        req = QTAILQ_FIRST(&ep->requests);
        if (req == NULL || !apple_sio_map_dma(ep, req)) {
            break;
        }
        iovec_len =
            qemu_iovec_to_buf(&req->iov, req->bytes_accessed, buffer, len);
        req->bytes_accessed += iovec_len;
        if (req->bytes_accessed >= req->iov.size) {
            apple_sio_dma_writeback(s, ep, req);
        }
        actual_len += iovec_len;
    }

    return actual_len;
}

uint64_t apple_sio_dma_write(AppleSIODMAEndpoint *ep, void *buffer,
                             uint64_t len)
{
    AppleSIOState *s;
    SIODMAMapRequest *req;
    uint64_t iovec_len;
    uint64_t actual_len = 0;

    QEMU_LOCK_GUARD(&ep->mutex);

    g_assert_cmpuint(ep->direction, ==, DMA_DIRECTION_FROM_DEVICE);

    s = container_of(ep, AppleSIOState, eps[ep->id]);

    while (len > actual_len) {
        req = QTAILQ_FIRST(&ep->requests);
        if (req == NULL || !apple_sio_map_dma(ep, req)) {
            break;
        }
        iovec_len =
            qemu_iovec_from_buf(&req->iov, req->bytes_accessed, buffer, len);
        req->bytes_accessed += iovec_len;
        if (req->bytes_accessed >= req->iov.size) {
            apple_sio_dma_writeback(s, ep, req);
        }
        actual_len += iovec_len;
    }

    return actual_len;
}

uint64_t apple_sio_dma_remaining(AppleSIODMAEndpoint *ep)
{
    uint64_t len;
    SIODMAMapRequest *req;
    SIODMAMapRequest *req_next;

    QEMU_LOCK_GUARD(&ep->mutex);

    len = 0;

    QTAILQ_FOREACH_SAFE (req, &ep->requests, next, req_next) {
        // using iov requires the request to be mapped.
        len += req->sgl.size - req->bytes_accessed;
    }

    return len;
}

static void apple_sio_control_get_param(AppleSIOState *s, SIOMessage *reply,
                                        uint32_t param)
{
    if (param == PARAM_PROTOCOL_VERSION) {
        reply->data = s->protocol_version;
        reply->op = OP_GET_PARAM_RESP;
    } else {
        reply->op = OP_SYNC_ERROR;
    }
}

static void apple_sio_control_set_param(AppleSIOState *s, SIOMessage *reply,
                                        uint32_t param, uint32_t value)
{
    switch (param) {
    case PARAM_SEGMENT_BASE:
        s->segment_base = value << 12;
        break;
    case PARAM_SEGMENT_SIZE:
        s->segment_size = value;
        break;
    }
    reply->op = OP_ACK;
}

static void apple_sio_control(AppleSIOState *s, AppleSIODMAEndpoint *ep,
                              SIOMessage *m)
{
    AppleRTKit *rtk;
    SIOMessage reply = { 0 };

    rtk = APPLE_RTKIT(s);

    QEMU_LOCK_GUARD(&ep->mutex);

    reply.ep = m->ep;
    reply.tag = m->tag;

    switch (m->op) {
    case OP_GET_PARAM:
        apple_sio_control_get_param(s, &reply, m->param);
        break;
    case OP_SET_PARAM:
        apple_sio_control_set_param(s, &reply, m->param, m->data);
        break;
    }

    apple_rtkit_send_user_msg(rtk, EP_CONTROL, reply.raw);
};

static void apple_sio_dma(AppleSIOState *s, AppleSIODMAEndpoint *ep,
                          SIOMessage m)
{
    AppleRTKit *rtk;
    SIOMessage reply = { 0 };
    dma_addr_t config_addr;
    dma_addr_t segment_addr;
    uint32_t segment_count;
    size_t i;
    SIODMAMapRequest *req;
    uint64_t bytes_accessed;

    QEMU_LOCK_GUARD(&ep->mutex);

    rtk = APPLE_RTKIT(s);

    reply.ep = m.ep;
    reply.tag = m.tag;

    switch (m.op) {
    case OP_CONFIGURE: {
        config_addr = s->segment_base + m.data * sizeof(SIODMASegment);
        if (dma_memory_read(&s->dma_as, config_addr, &ep->config,
                            sizeof(ep->config),
                            MEMTXATTRS_UNSPECIFIED) == MEMTX_OK) {
            reply.op = OP_ACK;
        } else {
            reply.op = OP_SYNC_ERROR;
        }
        break;
    }
    case OP_MAP: {
        segment_addr = s->segment_base + m.data * sizeof(SIODMASegment);
        if (dma_memory_read(&s->dma_as, segment_addr + 0x3C, &segment_count,
                            sizeof(segment_count),
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            reply.op = OP_SYNC_ERROR;
            break;
        }

        req = g_new0(SIODMAMapRequest, 1);
        req->segments = g_new(SIODMASegment, segment_count);
        if (dma_memory_read(&s->dma_as, segment_addr + 0x48, req->segments,
                            segment_count * sizeof(SIODMASegment),
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            g_free(req->segments);
            g_free(req);
            reply.op = OP_SYNC_ERROR;
            break;
        }

        qemu_sglist_init(&req->sgl, DEVICE(s), segment_count, &s->dma_as);
        req->tag = m.tag;
        req->segment_count = segment_count;
        for (i = 0; i < segment_count; ++i) {
            qemu_sglist_add(&req->sgl, req->segments[i].addr,
                            req->segments[i].len);
        }
        QTAILQ_INSERT_TAIL(&ep->requests, req, next);

        reply.op = OP_ACK;
        break;
    }
    case OP_QUERY:
        if (QTAILQ_EMPTY(&ep->requests)) {
            reply.op = OP_SYNC_ERROR;
            break;
        }

        bytes_accessed = 0;
        QTAILQ_FOREACH (req, &ep->requests, next) {
            bytes_accessed += req->bytes_accessed;
        }
        reply.op = OP_QUERY_OK;
        reply.data = bytes_accessed;
        break;
    case OP_STOP:
        apple_sio_stop(ep);
        reply.op = OP_ACK;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unknown SIO op: %d\n", __func__, m.op);
        reply.op = OP_SYNC_ERROR;
        break;
    }
    apple_rtkit_send_user_msg(rtk, EP_CONTROL, reply.raw);
};

static void apple_sio_handle_endpoint(void *opaque, uint8_t ep, uint64_t msg)
{
    AppleSIOState *sio = opaque;
    SIOMessage m = { 0 };

    m.raw = msg;

    SIO_LOG_MSG(ep, msg);

    switch (m.ep) {
    case EP_CONTROL:
    case EP_PERF:
        apple_sio_control(sio, &sio->eps[EP_CONTROL], &m);
        break;
    default:
        if (m.ep >= SIO_NUM_EPS) {
            qemu_log_mask(LOG_UNIMP, "%s: Unknown ep %X\n", __func__, m.ep);
        } else {
            apple_sio_dma(sio, &sio->eps[m.ep], m);
        }
        break;
    }
}

AppleSIODMAEndpoint *apple_sio_get_endpoint(AppleSIOState *s, int ep)
{
    if (ep <= EP_PERF || ep >= SIO_NUM_EPS) {
        return NULL;
    }

    return &s->eps[ep];
}

AppleSIODMAEndpoint *
apple_sio_get_endpoint_from_node(AppleSIOState *s, AppleDTNode *node, int idx)
{
    AppleDTProp *prop;
    uint32_t *data;
    int count;

    prop = apple_dt_get_prop(node, "dma-channels");
    if (prop == NULL) {
        return NULL;
    }

    count = prop->len / 32;
    if (idx >= count) {
        return NULL;
    }

    data = (uint32_t *)prop->data;

    return apple_sio_get_endpoint(s, data[8 * idx]);
}

static void ascv2_core_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
}

static uint64_t ascv2_core_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static const MemoryRegionOps ascv2_core_reg_ops = {
    .write = ascv2_core_reg_write,
    .read = ascv2_core_reg_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 8,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .valid.unaligned = false,
};

static void apple_sio_realize(DeviceState *dev, Error **errp)
{
    AppleSIOState *s;
    AppleSIOClass *sioc;
    Object *obj;

    s = APPLE_SIO(dev);
    sioc = APPLE_SIO_GET_CLASS(dev);

    if (sioc->parent_realize != NULL) {
        sioc->parent_realize(dev, errp);
    }

    obj = object_property_get_link(OBJECT(dev), "dma-mr", &error_abort);

    s->dma_mr = MEMORY_REGION(obj);
    g_assert_nonnull(s->dma_mr);
    address_space_init(&s->dma_as, s->dma_mr, "sio.dma-as");

    for (int i = 0; i < SIO_NUM_EPS; ++i) {
        s->eps[i].id = i;
        s->eps[i].direction =
            (i & 1) ? DMA_DIRECTION_FROM_DEVICE : DMA_DIRECTION_TO_DEVICE;
        qemu_mutex_init(&s->eps[i].mutex);
        QTAILQ_INIT(&s->eps[i].requests);
    }
}

static void apple_sio_reset_hold(Object *obj, ResetType type)
{
    AppleSIOState *s;
    AppleSIOClass *sioc;

    s = APPLE_SIO(obj);
    sioc = APPLE_SIO_GET_CLASS(obj);

    if (sioc->parent_reset.hold != NULL) {
        sioc->parent_reset.hold(obj, type);
    }

    s->segment_base = 0;
    s->segment_size = 0;

    for (size_t i = 0; i < SIO_NUM_EPS; ++i) {
        apple_sio_stop(&s->eps[i]);

        memset(&s->eps[i].config, 0, sizeof(s->eps[i].config));
    }
}

static const VMStateDescription vmstate_sio_dma_config = {
    .name = "SIODMAConfig",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32(xfer, SIODMAConfig),
            VMSTATE_UINT32(timeout, SIODMAConfig),
            VMSTATE_UINT32(fifo, SIODMAConfig),
            VMSTATE_UINT32(trigger, SIODMAConfig),
            VMSTATE_UINT32(fifo_depth, SIODMAConfig),
            VMSTATE_UINT32(field_14, SIODMAConfig),
            VMSTATE_UINT32(field_18, SIODMAConfig),
            VMSTATE_END_OF_LIST(),
        },
};

static const VMStateDescription vmstate_sio_dma_segment = {
    .name = "SIODMASegment",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT64(addr, SIODMASegment),
            VMSTATE_UINT32(len, SIODMASegment),
            VMSTATE_END_OF_LIST(),
        },
};

static int vmstate_apple_sio_dma_endpoint_pre_load(void *opaque)
{
    AppleSIODMAEndpoint *ep = opaque;

    apple_sio_stop(ep);

    return 0;
}

static int vmstate_apple_sio_dma_endpoint_post_load(void *opaque,
                                                    int version_id)
{
    AppleSIODMAEndpoint *ep = opaque;
    AppleSIOState *s;
    SIODMAMapRequest *req;
    uint64_t bytes_accessed;

    s = container_of(ep, AppleSIOState, eps[ep->id]);

    QTAILQ_FOREACH (req, &ep->requests, next) {
        qemu_sglist_init(&req->sgl, DEVICE(s), req->segment_count, &s->dma_as);
        for (uint32_t i = 0; i < req->segment_count; ++i) {
            qemu_sglist_add(&req->sgl, req->segments[i].addr,
                            req->segments[i].len);
        }

        if (req->mapped) {
            req->mapped = false;
            bytes_accessed = req->bytes_accessed;
            apple_sio_map_dma(ep, req);
            req->bytes_accessed = bytes_accessed;
        }
    }

    return 0;
}

static const VMStateDescription vmstate_sio_dma_map_req = {
    .name = "SIODMAMapRequest",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_STRUCT_VARRAY_UINT32_ALLOC(
                segments, SIODMAMapRequest, segment_count, 0,
                vmstate_sio_dma_segment, SIODMASegment),
            VMSTATE_UINT32(segment_count, SIODMAMapRequest),
            VMSTATE_UINT64(bytes_accessed, SIODMAMapRequest),
            VMSTATE_UINT32(tag, SIODMAMapRequest),
            VMSTATE_BOOL(mapped, SIODMAMapRequest),
            VMSTATE_END_OF_LIST(),
        },
};

static const VMStateDescription vmstate_apple_sio_dma_endpoint = {
    .name = "AppleSIODMAEndpoint",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_load = vmstate_apple_sio_dma_endpoint_pre_load,
    .post_load = vmstate_apple_sio_dma_endpoint_post_load,
    .fields =
        (const VMStateField[]){
            VMSTATE_STRUCT(config, AppleSIODMAEndpoint, 0,
                           vmstate_sio_dma_config, SIODMAConfig),
            VMSTATE_UINT32(id, AppleSIODMAEndpoint),
            VMSTATE_UINT32(direction, AppleSIODMAEndpoint),
            VMSTATE_QTAILQ_V(requests, AppleSIODMAEndpoint, 0,
                             vmstate_sio_dma_map_req, SIODMAMapRequest, next),
            VMSTATE_END_OF_LIST(),
        },
};

static const VMStateDescription vmstate_apple_sio = {
    .name = "AppleSIOState",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_APPLE_RTKIT(parent_obj, AppleSIOState),
            VMSTATE_STRUCT_ARRAY(eps, AppleSIOState, SIO_NUM_EPS, 0,
                                 vmstate_apple_sio_dma_endpoint,
                                 AppleSIODMAEndpoint),
            VMSTATE_UINT32(protocol_version, AppleSIOState),
            VMSTATE_UINT64(segment_base, AppleSIOState),
            VMSTATE_UINT32(segment_size, AppleSIOState),
            VMSTATE_END_OF_LIST(),
        },
};

static void apple_sio_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc;
    DeviceClass *dc;
    AppleSIOClass *sioc;

    rc = RESETTABLE_CLASS(klass);
    dc = DEVICE_CLASS(klass);
    sioc = APPLE_SIO_CLASS(klass);

    device_class_set_parent_realize(dc, apple_sio_realize,
                                    &sioc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, apple_sio_reset_hold, NULL,
                                       &sioc->parent_reset);
    dc->desc = "Apple Smart IO DMA Controller";
    dc->user_creatable = false;
    dc->vmsd = &vmstate_apple_sio;
}

static const TypeInfo apple_sio_info = {
    .name = TYPE_APPLE_SIO,
    .parent = TYPE_APPLE_RTKIT,
    .instance_size = sizeof(AppleSIOState),
    .class_size = sizeof(AppleSIOClass),
    .class_init = apple_sio_class_init,
};

static void apple_sio_register_types(void)
{
    type_register_static(&apple_sio_info);
}

type_init(apple_sio_register_types);

SysBusDevice *apple_sio_from_node(AppleDTNode *node, AppleA7IOPVersion version,
                                  uint32_t protocol_version)
{
    DeviceState *dev;
    AppleSIOState *s;
    SysBusDevice *sbd;
    AppleRTKit *rtk;
    AppleDTNode *child;
    AppleDTProp *prop;
    uint64_t *reg;

    dev = qdev_new(TYPE_APPLE_SIO);
    s = APPLE_SIO(dev);
    sbd = SYS_BUS_DEVICE(dev);
    rtk = APPLE_RTKIT(dev);

    dev->id = g_strdup("sio");

    s->protocol_version = protocol_version;

    child = apple_dt_get_node(node, "iop-sio-nub");
    g_assert_nonnull(child);

    prop = apple_dt_get_prop(node, "reg");
    g_assert_nonnull(prop);

    reg = (uint64_t *)prop->data;

    apple_rtkit_init(rtk, NULL, "SIO", reg[1], version, NULL);
    apple_rtkit_register_user_ep(rtk, 0, s, apple_sio_handle_endpoint);

    memory_region_init_io(&s->ascv2_iomem, OBJECT(dev), &ascv2_core_reg_ops, s,
                          TYPE_APPLE_SIO ".ascv2-core-reg", reg[3]);
    sysbus_init_mmio(sbd, &s->ascv2_iomem);

    apple_dt_set_prop_u32(child, "pre-loaded", 1);
    apple_dt_set_prop_u32(child, "running", 1);

    return sbd;
}
