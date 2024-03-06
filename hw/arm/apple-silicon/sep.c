/*
 * Apple SEP.
 *
 * Copyright (c) 2023 Visual Ehrmanntraut.
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
#include "hw/arm/apple-silicon/boot.h"
#include "hw/arm/apple-silicon/sep.h"
#include "hw/misc/apple-silicon/a7iop/core.h"
#include "hw/misc/apple-silicon/a7iop/mailbox/core.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "sysemu/dma.h"

typedef enum {
    SEP_STATUS_SLEEPING = 0,
    SEP_STATUS_BOOTSTRAP = 1,
    SEP_STATUS_ACTIVE = 2,
} AppleSEPStatus;

typedef struct {
    uint8_t ep;
    uint8_t tag;
    uint8_t op;
    uint8_t param;
    uint32_t data;
} QEMU_PACKED SEPMessage;

typedef struct {
    uint8_t ep;
    uint8_t tag;
    uint8_t op;
    uint8_t id;
    uint32_t name;
} QEMU_PACKED EPAdvertisementMessage;

typedef struct {
    uint8_t ep;
    uint8_t tag;
    uint8_t op;
    uint8_t id;
    AppleSEPOOLInfo ool_info;
} QEMU_PACKED OOLAdvertisementMessage;

typedef struct {
    uint8_t ep;
    uint8_t tag;
    uint16_t size;
    uint32_t address;
} QEMU_PACKED L4InfoMessage;

typedef struct {
    uint8_t ep;
    uint8_t tag;
    uint8_t op;
    uint8_t id;
    uint32_t data;
} QEMU_PACKED SetOOLMessage;

enum {
    EP_CONTROL = 0, // 'cntl'
    EP_LOGGER = 1, // 'log '
    EP_ART_STORAGE = 2, // 'arts'
    EP_ART_REQUESTS = 3, // 'artr'
    EP_TRACING = 4, // 'trac'
    EP_DEBUG = 5, // 'debu'
    EP_EMBEDDED_ISP = 6, // 'eisp'
    EP_MOBILE_SKS = 7, // 'msks'
    EP_SECURE_BIOMETRICS = 8, // 'sbio'
    EP_FACE_ID = 9, // 'sprl'
    EP_SECURE_CREDENTIALS = 10, // 'scrd'
    EP_PAIRING = 11,
    EP_SECURE_ELEMENT = 12, // 'sse '
    // 13 = ??
    EP_HDCP = 14, // 'hdcp'
    EP_UNIT_TESTING = 15, // 'unit'
    EP_XART_SLAVE = 16, // 'xars'
    EP_HILO = 17, // 'hilo'
    EP_KEYSTORE = 18, // 'sks '
    EP_XART_MASTER = 19, // 'xarm'
    EP_SMC = 20, // 'smc '
    EP_HIBERNATION = 20, // 'hibe'
    EP_NONP = 21, // 'nonp'
    EP_CYRS = 22, // 'cyrs'
    EP_SKDL = 23, // 'skdl'
    EP_STAC = 24, // 'stac'
    EP_SIDV = 25, // 'sidv'
    EP_DISCOVERY = 253,
    EP_L4INFO = 254,
    EP_BOOTSTRAP = 255,
};

enum {
    CONTROL_OP_NOP = 0,
    CONTROL_OP_ACK = 1,
    CONTROL_OP_SET_OOL_IN_ADDR = 2,
    CONTROL_OP_SET_OOL_OUT_ADDR = 3,
    CONTROL_OP_SET_OOL_IN_SIZE = 4,
    CONTROL_OP_SET_OOL_OUT_SIZE = 5,
    CONTROL_OP_TTY_IN = 10,
    CONTROL_OP_SLEEP = 12,
    CONTROL_OP_NOTIFY_ALIVE = 13,
    CONTROL_OP_NAP = 19,
    CONTROL_OP_GET_SECURITY_MODE = 20,
    CONTROL_OP_SELF_TEST = 24,
    CONTROL_OP_SET_DMA_CMD_ADDR = 25,
    CONTROL_OP_SET_DMA_CMD_SIZE = 26,
    CONTROL_OP_SET_DMA_IN_ADDR = 27,
    CONTROL_OP_SET_DMA_OUT_ADDR = 28,
    CONTROL_OP_SET_DMA_IN_RELAY_ADDR = 29,
    CONTROL_OP_SET_DMA_OUT_RELAY_ADDR = 30,
    CONTROL_OP_SET_DMA_IN_SIZE = 31,
    CONTROL_OP_SET_DMA_OUT_SIZE = 32,
    CONTROL_OP_ERASE_INSTALL = 37,
    CONTROL_OP_L4_PANIC = 38,
    CONTROL_OP_SEP_OS_PANIC = 39,
};

enum {
    LOGGER_OP_UPDATE_POSITION = 11,
};

enum {
    ART_STORAGE_OP_SEND_ART = 20,
    ART_STORAGE_OP_ART_RECEIVED = 21,
};

enum {
    ART_REQUESTS_OP_NEW_NONCE = 20,
    ART_REQUESTS_OP_INVALIDATE_NONCE = 21,
    ART_REQUESTS_OP_COMMIT_HASH = 22,
    ART_REQUESTS_OP_COUNTER_SELF_TEST = 30,
    ART_REQUESTS_OP_PURGE_SYSTEM_TOKEN = 40,
};

enum {
    DEBUG_OP_COPY_FROM_OBJECT = 0,
    DEBUG_OP_COPY_TO_OBJECT = 1,
    DEBUG_OP_OBJECT_INFO = 2,
    DEBUG_OP_CREATE_OBJECT = 3,
    DEBUG_OP_SHARE_OBJECT = 4,
    DEBUG_OP_DUMP_TRNG_DATA = 5,
    DEBUG_OP_PROCESS_INFO = 6,
    DEBUG_OP_DUMP_COVERAGE = 7,
};

enum {
    // IOP -> AP
    XART_OP_ACK = 0,
    XART_OP_GET_XART = 0,
    XART_OP_SET_XART = 1,
    XART_OP_GET_LOCKER_REDORD = 5,
    XART_OP_ADD_LOCKER_RECORD = 6,
    XART_OP_DELETE_LOCKER_RECORD = 7,
    XART_OP_LYNX_AUTHENTICATE = 9,
    XART_OP_LYNX_GET_CPSN = 10,
    XART_OP_LYNX_GET_PUBLIC_KEY = 11,
    XART_OP_FLUSH_CACHED_XART = 12,
    XART_OP_SHUTDOWN = 13,
    // AP -> IOP
    XART_OP_NONCE_GENERATE = 15,
    XART_OP_NONCE_READ = 16,
    XART_OP_NONCE_INVALIDATE = 17,
    XART_OP_COMMIT_HASH = 18,
};

enum {
    DISCOVERY_OP_EP_ADVERT = 0,
    DISCOVERY_OP_OOL_ADVERT = 1,
};

enum {
    BOOTSTRAP_OP_PING = 1,
    BOOTSTRAP_OP_GET_STATUS = 2,
    BOOTSTRAP_OP_GENERATE_NONCE = 3,
    BOOTSTRAP_OP_GET_NONCE_WORD = 4,
    BOOTSTRAP_OP_CHECK_TZ0 = 5,
    BOOTSTRAP_OP_BOOT_IMG4 = 6,
    BOOTSTRAP_OP_LOAD_SEP_ART = 7,
    BOOTSTRAP_OP_NOTIFY_OS_ACTIVE_ASYNC = 13,
    BOOTSTRAP_OP_SEND_DPA = 15,
    BOOTSTRAP_OP_NOTIFY_OS_ACTIVE = 21,
    BOOTSTRAP_OP_PING_ACK = 101,
    BOOTSTRAP_OP_STATUS_REPLY = 102,
    BOOTSTRAP_OP_NONCE_GENERATED = 103,
    BOOTSTRAP_OP_NONCE_WORD_REPLY = 104,
    BOOTSTRAP_OP_TZ0_ACCEPTED = 105,
    BOOTSTRAP_OP_IMG4_ACCEPTED = 106,
    BOOTSTRAP_OP_ART_ACCEPTED = 107,
    BOOTSTRAP_OP_RESUMED_FROM_RAM = 108,
    BOOTSTRAP_OP_DPA_SENT = 115,
    BOOTSTRAP_OP_LOG_RAW = 201,
    BOOTSTRAP_OP_LOG_PRINTABLE = 202,
    BOOTSTRAP_OP_ANNOUNCE_STATUS = 210,
    BOOTSTRAP_OP_PANIC = 255,
};

static void apple_sep_set_ool_in_size(AppleSEPState *s, uint8_t ep,
                                      uint32_t size)
{
    g_assert(ep < SEP_ENDPOINT_MAX);
    s->ool_state[ep].in_size = size;
}

static void apple_sep_set_ool_in_addr(AppleSEPState *s, uint8_t ep,
                                      uint64_t addr)
{
    g_assert(ep < SEP_ENDPOINT_MAX);
    s->ool_state[ep].in_addr = addr;
}

static void apple_sep_set_ool_out_size(AppleSEPState *s, uint8_t ep,
                                       uint32_t size)
{
    g_assert(ep < SEP_ENDPOINT_MAX);
    s->ool_state[ep].out_size = size;
}

static void apple_sep_set_ool_out_addr(AppleSEPState *s, uint8_t ep,
                                       uint64_t addr)
{
    g_assert(ep < SEP_ENDPOINT_MAX);
    s->ool_state[ep].out_addr = addr;
}

static void apple_sep_send_generic_message(AppleSEPState *s, uint8_t ep,
                                           uint8_t tag, uint8_t op,
                                           uint8_t param, uint32_t data)
{
    AppleA7IOP *a7iop;
    AppleA7IOPMessage *sent_msg;
    SEPMessage *sent_sep_msg;

    a7iop = APPLE_A7IOP(s);

    sent_msg = g_new0(AppleA7IOPMessage, 1);
    sent_sep_msg = (SEPMessage *)sent_msg->data;
    sent_sep_msg->ep = ep;
    sent_sep_msg->tag = tag;
    sent_sep_msg->op = op;
    sent_sep_msg->param = param;
    sent_sep_msg->data = data;
    apple_a7iop_send_ap(a7iop, sent_msg);
}

static void apple_sep_control_ack(AppleSEPState *s, SEPMessage *msg,
                                  uint8_t param, uint32_t data)
{
    apple_sep_send_generic_message(s, EP_CONTROL, msg->tag, CONTROL_OP_ACK,
                                   param, data);
}

static void apple_sep_handle_control_msg(AppleSEPState *s, SEPMessage *msg)
{
    SetOOLMessage *set_ool_msg;

    switch (msg->op) {
    case CONTROL_OP_NOP:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_CONTROL: NOP\n");
        apple_sep_control_ack(s, msg, 0, 0);
        break;
    case CONTROL_OP_SET_OOL_IN_ADDR:
        set_ool_msg = (SetOOLMessage *)msg;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "EP_CONTROL: SET_OOL_IN_ADDR (%d, 0x%llX)\n",
                      set_ool_msg->id, (uint64_t)set_ool_msg->data << 12);
        apple_sep_set_ool_in_addr(s, set_ool_msg->id,
                                  (uint64_t)set_ool_msg->data << 12);
        apple_sep_control_ack(s, msg, 0, 0);
        break;
    case CONTROL_OP_SET_OOL_OUT_ADDR:
        set_ool_msg = (SetOOLMessage *)msg;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "EP_CONTROL: SET_OOL_OUT_ADDR (%d, 0x%llX)\n",
                      set_ool_msg->id, (uint64_t)set_ool_msg->data << 12);
        apple_sep_set_ool_out_addr(s, set_ool_msg->id,
                                   (uint64_t)set_ool_msg->data << 12);
        apple_sep_control_ack(s, msg, 0, 0);
        break;
    case CONTROL_OP_SET_OOL_IN_SIZE:
        set_ool_msg = (SetOOLMessage *)msg;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "EP_CONTROL: SET_OOL_IN_SIZE (%d, 0x%X)\n",
                      set_ool_msg->id, set_ool_msg->data);
        apple_sep_set_ool_in_size(s, set_ool_msg->id, set_ool_msg->data);
        apple_sep_control_ack(s, msg, 0, 0);
        break;
    case CONTROL_OP_SET_OOL_OUT_SIZE:
        set_ool_msg = (SetOOLMessage *)msg;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "EP_CONTROL: SET_OOL_OUT_SIZE (%d, 0x%X)\n",
                      set_ool_msg->id, set_ool_msg->data);
        apple_sep_set_ool_out_size(s, set_ool_msg->id, set_ool_msg->data);
        apple_sep_control_ack(s, msg, 0, 0);
        break;
    case CONTROL_OP_GET_SECURITY_MODE:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_CONTROL: GET_SECURITY_MODE\n");
        apple_sep_control_ack(s, msg, 0, 3);
        break;
    case CONTROL_OP_SELF_TEST:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_CONTROL: SELF_TEST\n");
        apple_sep_control_ack(s, msg, 0, 0);
        break;
    case CONTROL_OP_ERASE_INSTALL:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_CONTROL: ERASE_INSTALL\n");
        apple_sep_control_ack(s, msg, 0, 0);
        //! TODO: Actually generate this
        uint8_t art[] = {};
        if (dma_memory_write(s->dma_as, s->ool_state[EP_ART_STORAGE].out_addr,
                             art, sizeof(art),
                             MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "EP_XART_STORAGE: Failed to write ART to OOL");
        }
        apple_sep_send_generic_message(s, EP_ART_STORAGE, 0,
                                       ART_STORAGE_OP_SEND_ART, 0, 0);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_CONTROL: Unknown opcode %d\n",
                      msg->op);
        break;
    }
}

static void apple_sep_handle_arts_msg(AppleSEPState *s, SEPMessage *msg)
{
    switch (msg->op) {
    case ART_STORAGE_OP_ART_RECEIVED:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_ART_STORAGE: ART_RECEIVED\n");
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_ART_STORAGE: Unknown opcode %d\n",
                      msg->op);
        break;
    }
}

static void apple_sep_xart_ack(AppleSEPState *s, SEPMessage *msg, uint8_t param,
                               uint32_t data)
{
    apple_sep_send_generic_message(s, msg->ep, msg->tag, XART_OP_ACK, param,
                                   data);
}

static void apple_sep_handle_xart_msg(AppleSEPState *s, bool slave,
                                      SEPMessage *msg)
{
    const char *ep_name;

    ep_name = slave ? "SLAVE" : "MASTER";

    switch (msg->op) {
    case XART_OP_FLUSH_CACHED_XART:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_XART_%s: FLUSH_CACHED_XART\n",
                      ep_name);
        apple_sep_xart_ack(s, msg, 0, 0);
        break;
    case XART_OP_COMMIT_HASH:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_XART_%s: COMMIT_HASH\n", ep_name);
        apple_sep_xart_ack(s, msg, 0, 0);
        break;
    case XART_OP_NONCE_GENERATE:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_XART_%s: NONCE_GENERATE\n", ep_name);
        apple_sep_xart_ack(s, msg, 0, 0);
        break;
    case XART_OP_NONCE_INVALIDATE:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_XART_%s: NONCE_INVALIDATE\n",
                      ep_name);
        apple_sep_xart_ack(s, msg, 0, 0);
        break;
    case XART_OP_SHUTDOWN:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_XART_%s: SHUTDOWN\n", ep_name);
        apple_sep_xart_ack(s, msg, 0, 0);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_XART_%s: Unknown opcode %d\n",
                      ep_name, msg->op);
        apple_sep_xart_ack(s, msg, 0, 0);
        break;
    }
}

static void apple_sep_handle_l4info(AppleSEPState *s, L4InfoMessage *msg)
{
    qemu_log_mask(LOG_GUEST_ERROR, "EP_L4INFO: address 0x%llX size 0x%X\n",
                  (uint64_t)msg->address << 12, msg->size << 12);
    s->ool_state[EP_CONTROL].in_addr = (uint64_t)msg->address << 12;
    s->ool_state[EP_CONTROL].in_size = msg->size << 12;
    s->ool_state[EP_CONTROL].out_addr = (uint64_t)msg->address << 12;
    s->ool_state[EP_CONTROL].out_size = msg->size << 12;
}

static const uint8_t apple_sep_eps[] = {
    EP_CONTROL,    EP_ART_STORAGE, EP_ART_REQUESTS, EP_SECURE_CREDENTIALS,
    EP_XART_SLAVE, EP_KEYSTORE,    EP_XART_MASTER,
};
static const uint32_t apple_sep_endpoint_names[] = {
    'cntl', 'arts', 'artr', 'scrd', 'xars', 'sks ', 'xarm',
};

static void apple_sep_advertise_eps(AppleSEPState *s)
{
    AppleA7IOP *a7iop;
    AppleA7IOPMessage *msg;
    EPAdvertisementMessage *ep_advert_msg;
    OOLAdvertisementMessage *ool_advert_msg;
    size_t i;

    a7iop = APPLE_A7IOP(s);

    for (i = 0; i < (sizeof(apple_sep_eps) / sizeof(*apple_sep_eps)); i++) {
        msg = g_new0(AppleA7IOPMessage, 1);
        ep_advert_msg = (EPAdvertisementMessage *)msg->data;
        ep_advert_msg->ep = EP_DISCOVERY;
        ep_advert_msg->op = DISCOVERY_OP_EP_ADVERT;
        ep_advert_msg->id = apple_sep_eps[i];
        ep_advert_msg->name = apple_sep_endpoint_names[i];
        apple_a7iop_send_ap(a7iop, msg);

        msg = g_new0(AppleA7IOPMessage, 1);
        ool_advert_msg = (OOLAdvertisementMessage *)msg->data;
        ool_advert_msg->ep = EP_DISCOVERY;
        ool_advert_msg->op = DISCOVERY_OP_OOL_ADVERT;
        ool_advert_msg->id = apple_sep_eps[i];
        memcpy(&ool_advert_msg->ool_info, s->ool_info + apple_sep_eps[i],
               sizeof(AppleSEPOOLInfo));
        apple_a7iop_send_ap(a7iop, msg);
    }
}

static void apple_sep_handle_bootstrap_msg(AppleSEPState *s, SEPMessage *msg)
{
    switch (msg->op) {
    case BOOTSTRAP_OP_GET_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_BOOTSTRAP: GET_STATUS\n");

        apple_sep_send_generic_message(s, EP_BOOTSTRAP, msg->tag,
                                       BOOTSTRAP_OP_STATUS_REPLY, 0, s->status);
        break;
    case BOOTSTRAP_OP_CHECK_TZ0:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_BOOTSTRAP: CHECK_TZ0\n");

        s->rsep = msg->param == 1;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "EP_BOOTSTRAP: TrustZone 0 is totally OK, trust me. "
                      "Firmware type: %s\n",
                      s->rsep ? "rsep" : "sepi");
        s->status = SEP_STATUS_ACTIVE;

        apple_sep_send_generic_message(s, EP_BOOTSTRAP, msg->tag,
                                       BOOTSTRAP_OP_TZ0_ACCEPTED, 0, 0);
        break;
    case BOOTSTRAP_OP_BOOT_IMG4: {
        qemu_log_mask(LOG_GUEST_ERROR, "EP_BOOTSTRAP: BOOT_IMG4\n");

        g_assert(s->rsep == (msg->param == 1));

        apple_sep_send_generic_message(s, EP_BOOTSTRAP, msg->tag,
                                       BOOTSTRAP_OP_IMG4_ACCEPTED, 0, 0);
        apple_sep_send_generic_message(s, EP_CONTROL, 0,
                                       CONTROL_OP_NOTIFY_ALIVE, 0, 0);
        apple_sep_advertise_eps(s);
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "EP_BOOTSTRAP: Unknown opcode %d\n",
                      msg->op);
        break;
    }
}

static void apple_sep_bh(void *opaque)
{
    AppleSEPState *s;
    AppleA7IOP *a7iop;
    AppleA7IOPMessage *msg;
    SEPMessage *sep_msg;

    s = APPLE_SEP(opaque);
    a7iop = APPLE_A7IOP(opaque);

    QEMU_LOCK_GUARD(&s->lock);

    while (!apple_a7iop_mailbox_is_empty(a7iop->iop_mailbox)) {
        msg = apple_a7iop_recv_iop(a7iop);
        sep_msg = (SEPMessage *)msg->data;

        switch (sep_msg->ep) {
        case EP_CONTROL:
            apple_sep_handle_control_msg(s, sep_msg);
            break;
        case EP_ART_STORAGE:
            apple_sep_handle_arts_msg(s, sep_msg);
            break;
        case EP_ART_REQUESTS:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "EP_ART_REQUESTS: Unknown opcode %d\n", sep_msg->op);
            break;
        case EP_SECURE_CREDENTIALS:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "EP_SECURE_CREDENTIALS: Unknown opcode %d\n",
                          sep_msg->op);
            break;
        case EP_XART_SLAVE:
            apple_sep_handle_xart_msg(s, true, sep_msg);
            break;
        case EP_KEYSTORE:
            qemu_log_mask(LOG_GUEST_ERROR, "EP_KEYSTORE: Unknown opcode %d\n",
                          sep_msg->op);
            break;
        case EP_XART_MASTER:
            apple_sep_handle_xart_msg(s, false, sep_msg);
            break;
        case EP_DISCOVERY:
            qemu_log_mask(LOG_GUEST_ERROR, "EP_DISCOVERY: Unknown opcode %d\n",
                          sep_msg->op);
            break;
        case EP_L4INFO:
            apple_sep_handle_l4info(s, (L4InfoMessage *)sep_msg);
            break;
        case EP_BOOTSTRAP:
            apple_sep_handle_bootstrap_msg(s, sep_msg);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "UNKNOWN_%d_OP_%d\n", sep_msg->ep,
                          sep_msg->op);
            break;
        }

        g_free(msg);
    }
}

AppleSEPState *apple_sep_create(DTBNode *node, bool modern)
{
    DeviceState *dev;
    AppleA7IOP *a7iop;
    AppleSEPState *s;
    DTBProp *prop;
    uint64_t *reg;

    dev = qdev_new(TYPE_APPLE_SEP);
    a7iop = APPLE_A7IOP(dev);
    s = APPLE_SEP(dev);

    prop = find_dtb_prop(node, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;

    apple_a7iop_init(a7iop, "SEP", reg[1],
                     modern ? APPLE_A7IOP_V4 : APPLE_A7IOP_V2, NULL,
                     qemu_bh_new(apple_sep_bh, s));

    qemu_mutex_init(&s->lock);

    DTBNode *child = find_dtb_node(node, "iop-sep-nub");
    g_assert(child);
    remove_dtb_node_by_name(child, "Lynx");
    return s;
}

static void apple_sep_realize(DeviceState *dev, Error **errp)
{
    AppleSEPState *s;
    AppleSEPClass *sc;

    s = APPLE_SEP(dev);
    sc = APPLE_SEP_GET_CLASS(dev);
    if (sc->parent_realize) {
        sc->parent_realize(dev, errp);
    }
}

static void apple_sep_reset(DeviceState *dev)
{
    AppleSEPState *s;
    AppleSEPClass *sc;
    AppleA7IOP *a7iop;
    size_t i;
    AppleA7IOPMessage *msg;
    SEPMessage *sep_msg;

    s = APPLE_SEP(dev);
    sc = APPLE_SEP_GET_CLASS(dev);
    a7iop = APPLE_A7IOP(dev);
    if (sc->parent_reset) {
        sc->parent_reset(dev);
    }

    QEMU_LOCK_GUARD(&s->lock);

    a7iop->iop_mailbox->ap_dir_en = true;
    a7iop->iop_mailbox->iop_dir_en = true;
    a7iop->ap_mailbox->iop_dir_en = true;
    a7iop->ap_mailbox->ap_dir_en = true;

    for (i = 0; i < (sizeof(apple_sep_eps) / sizeof(*apple_sep_eps)); i++) {
        switch (apple_sep_eps[i]) {
        case EP_LOGGER:
            s->ool_info[apple_sep_eps[i]].in_max_pages = 0;
            s->ool_info[apple_sep_eps[i]].in_min_pages = 0;
            s->ool_info[apple_sep_eps[i]].out_max_pages = 1;
            s->ool_info[apple_sep_eps[i]].out_min_pages = 1;
            break;
        case EP_ART_STORAGE:
        case EP_ART_REQUESTS:
        case EP_DEBUG:
        case EP_UNIT_TESTING:
            s->ool_info[apple_sep_eps[i]].in_max_pages = 1;
            s->ool_info[apple_sep_eps[i]].in_min_pages = 1;
            s->ool_info[apple_sep_eps[i]].out_max_pages = 1;
            s->ool_info[apple_sep_eps[i]].out_min_pages = 1;
            break;
        case EP_HILO:
            s->ool_info[apple_sep_eps[i]].in_max_pages = 0;
            s->ool_info[apple_sep_eps[i]].in_min_pages = 0;
            s->ool_info[apple_sep_eps[i]].out_max_pages = 0;
            s->ool_info[apple_sep_eps[i]].out_min_pages = 0;
            break;
        default:
            s->ool_info[apple_sep_eps[i]].in_max_pages = 2;
            s->ool_info[apple_sep_eps[i]].in_min_pages = 2;
            s->ool_info[apple_sep_eps[i]].out_max_pages = 2;
            s->ool_info[apple_sep_eps[i]].out_min_pages = 2;
            break;
        }
    }

    s->status = SEP_STATUS_BOOTSTRAP;

    msg = g_new0(AppleA7IOPMessage, 1);
    sep_msg = (SEPMessage *)msg->data;
    sep_msg->ep = EP_BOOTSTRAP;
    sep_msg->op = BOOTSTRAP_OP_ANNOUNCE_STATUS;
    sep_msg->data = s->status;
    apple_a7iop_send_ap(a7iop, msg);
}

static void apple_sep_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AppleSEPClass *sc = APPLE_SEP_CLASS(klass);
    device_class_set_parent_realize(dc, apple_sep_realize, &sc->parent_realize);
    device_class_set_parent_reset(dc, apple_sep_reset, &sc->parent_reset);
    dc->desc = "Apple SEP";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo apple_sep_info = {
    .name = TYPE_APPLE_SEP,
    .parent = TYPE_APPLE_A7IOP,
    .instance_size = sizeof(AppleSEPState),
    .class_size = sizeof(AppleSEPClass),
    .class_init = apple_sep_class_init,
};

static void apple_sep_register_types(void)
{
    type_register_static(&apple_sep_info);
}

type_init(apple_sep_register_types);
