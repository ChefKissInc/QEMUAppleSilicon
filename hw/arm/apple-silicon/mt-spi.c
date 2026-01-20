/*
 * Apple Multi Touch SPI Controller.
 *
 * Copyright (c) 2025-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
 * Copyright (c) 2025-2026 Christian Inci (chris-pcguy).
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
#include "block/aio.h"
#include "hw/arm/apple-silicon/mt-spi.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "qemu/crc16.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "ui/console.h"
#include "ui/input.h"

typedef struct AppleMTSPIBuffer {
    uint8_t *data;
    uint32_t capacity;
    uint32_t len;
    uint32_t read_pos;
} AppleMTSPIBuffer;

typedef struct AppleMTSPILLPacket {
    AppleMTSPIBuffer buf;
    uint8_t type;
    QTAILQ_ENTRY(AppleMTSPILLPacket) next;
} AppleMTSPILLPacket;

struct AppleMTSPIState {
    SSIPeripheral parent_obj;

    QemuMutex lock;
    /// IRQ of the Multi Touch Controller is Active Low.
    /// qemu_irq_raise means IRQ inactive,
    /// qemu_irq_lower means IRQ active.
    qemu_irq irq;
    AppleMTSPIBuffer tx;
    AppleMTSPIBuffer rx;
    AppleMTSPIBuffer pending_hbpp;
    QTAILQ_HEAD(, AppleMTSPILLPacket) pending_fw;
    uint8_t frame;
    QEMUTimer *timer;
    QEMUTimer *end_timer;
    int16_t x;
    int16_t y;
    int16_t prev_x;
    int16_t prev_y;
    uint64_t prev_ts;
    int32_t btn_state;
    int32_t prev_btn_state;
    uint32_t display_width;
    uint32_t display_height;
};

// HBPP Command:
// u8 packet_type
// u1 unk0
// u3 packet_size? // 0 = Empty
// u4 unk1

#define HBPP_PACKET_RESET (0x00)
#define HBPP_PACKET_NOP (0x18)
#define HBPP_PACKET_INT_ACK (0x1A)
#define HBPP_PACKET_MEM_READ (0x1C)
#define HBPP_PACKET_MEM_RMW (0x1E)
#define HBPP_PACKET_REQ_CAL (0x1F)
#define HBPP_PACKET_DATA (0x30)

#define HBPP_PACKET_REQ_BOOT (0x011F)
#define HBPP_PACKET_ACK_RD_REQ (0x394C)
#define HBPP_PACKET_ACK_NOP (0x7948)
#define HBPP_PACKET_CAL_DONE (0x6949)
#define HBPP_PACKET_ACK_DATA (0xC14B)
#define HBPP_PACKET_ACK_WR_REQ (0xD14A)

#define LL_PACKET_PREAMBLE (0xDEADBEEF)
#define LL_PACKET_LEN (0x204)

#define LL_PACKET_LOSSLESS_OUTPUT (0x10)
#define LL_PACKET_LOSSY_OUTPUT (0x11)
#define LL_PACKET_LOSSLESS_INPUT (0x20)
#define LL_PACKET_LOSSY_INPUT (0x21)
#define LL_PACKET_CONTROL (0x40)
#define LL_PACKET_NO_DATA (0x80)

#define LL_PACKET_ERROR (0xB3655245)
#define LL_PACKET_ACK (0xD56827AC)
#define LL_PACKET_NAK (0xE4FB139)
#define LL_PACKET_BUSY (0xF8E5179C)

#define HID_CONTROL_PACKET_GET_RESULT_DATA (0x10)
#define HID_CONTROL_PACKET_SET_RESULT_DATA (0x20)
#define HID_CONTROL_PACKET_GET_INPUT_REPORT (0x30)
#define HID_CONTROL_PACKET_GET_OUTPUT_REPORT (0x31)
#define HID_CONTROL_PACKET_GET_FEATURE_REPORT (0x32)
#define HID_CONTROL_PACKET_SET_INPUT_REPORT (0x50)
#define HID_CONTROL_PACKET_SET_OUTPUT_REPORT (0x51)
#define HID_CONTROL_PACKET_SET_FEATURE_REPORT (0x52)

#define HID_TRANSFER_PACKET_INPUT (0x10)
#define HID_TRANSFER_PACKET_OUTPUT (0x20)

#define HID_PACKET_STATUS_SUCCESS (0)
#define HID_PACKET_STATUS_BUSY (1)
#define HID_PACKET_STATUS_ERROR (2)
#define HID_PACKET_STATUS_ERROR_ID_MISMATCH (3)
#define HID_PACKET_STATUS_ERROR_UNSUPPORTED (4)
#define HID_PACKET_STATUS_ERROR_INCORRECT_LENGTH (5)

#define HID_REPORT_BINARY_PATH_OR_IMAGE (0x44)
#define HID_REPORT_SENSOR_REGION_PARAM (0xA1)
#define HID_REPORT_SENSOR_REGION_DESC (0xD0)
#define HID_REPORT_FAMILY_ID (0xD1)
#define HID_REPORT_BASIC_DEVICE_INFO (0xD3)
#define HID_REPORT_BUTTONS (0xD7)
#define HID_REPORT_SENSOR_SURFACE_DESC (0xD9)
#define HID_REPORT_POWER_STATS (0x72)
#define HID_REPORT_POWER_STATS_DESC (0x73)
#define HID_REPORT_STATUS (0x7F)

// maybe 0xc2?
#define MT_FAMILY_ID (0xC3)
#define MT_LITTLE_ENDIAN (1)
#define MT_ROWS (31)
#define MT_COLUMNS (15)
#define MT_BCD_VER (bswap16(0x292))
#define MT_SENSOR_SURFACE_WIDTH (6458) // display_width/828 * 7.8
#define MT_SENSOR_SURFACE_HEIGHT (13977) // display_height/1792 * 7.8

#define PATH_STAGE_NOT_TRACKING (0)
#define PATH_STAGE_START_IN_RANGE (1)
#define PATH_STAGE_HOVER_IN_RANGE (2)
#define PATH_STAGE_MAKE_TOUCH (3)
#define PATH_STAGE_TOUCHING (4)
#define PATH_STAGE_BREAK_TOUCH (5)
#define PATH_STAGE_LINGER_IN_RANGE (6)
#define PATH_STAGE_OUT_OF_RANGE (7)

static void apple_mt_spi_buf_free(AppleMTSPIBuffer *buf)
{
    g_free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

static void apple_mt_spi_buf_ensure_capacity(AppleMTSPIBuffer *buf,
                                             size_t bytes)
{
    if ((buf->len + bytes) > buf->capacity) {
        buf->capacity = buf->len + bytes;
        buf->data = g_realloc(buf->data, buf->capacity);
    }
}

static void apple_mt_spi_buf_set_capacity(AppleMTSPIBuffer *buf,
                                          size_t capacity)
{
    g_assert_cmphex(capacity, >=, buf->capacity);
    buf->capacity = capacity;
    buf->data = g_realloc(buf->data, buf->capacity);
}

static void apple_mt_spi_buf_set_len(AppleMTSPIBuffer *buf, uint8_t val,
                                     size_t len)
{
    g_assert_cmphex(len, >=, buf->len);
    apple_mt_spi_buf_ensure_capacity(buf, len - buf->len);
    memset(buf->data + buf->len, val, len - buf->len);
    buf->len = len;
}

static bool apple_mt_spi_buf_is_empty(const AppleMTSPIBuffer *buf)
{
    return buf->len == 0;
}

static size_t apple_mt_spi_buf_get_pos(const AppleMTSPIBuffer *buf)
{
    g_assert_false(apple_mt_spi_buf_is_empty(buf));
    return buf->len - 1;
}

static bool apple_mt_spi_buf_is_full(const AppleMTSPIBuffer *buf)
{
    return !apple_mt_spi_buf_is_empty(buf) && buf->len == buf->capacity;
}

static bool apple_mt_spi_buf_pos_at_start(const AppleMTSPIBuffer *buf)
{
    return apple_mt_spi_buf_get_pos(buf) == 0;
}

static bool apple_mt_spi_buf_read_pos_at_end(const AppleMTSPIBuffer *buf)
{
    return (buf->read_pos + 1) == buf->len;
}

static void apple_mt_spi_buf_push_byte(AppleMTSPIBuffer *buf, uint8_t val)
{
    apple_mt_spi_buf_ensure_capacity(buf, sizeof(val));
    buf->data[buf->len] = val;
    buf->len += sizeof(val);
}

static void apple_mt_spi_buf_push_word(AppleMTSPIBuffer *buf, uint16_t val)
{
    apple_mt_spi_buf_ensure_capacity(buf, sizeof(val));
    stw_le_p(buf->data + buf->len, val);
    buf->len += sizeof(val);
}

static void apple_mt_spi_buf_push_dword(AppleMTSPIBuffer *buf, uint32_t val)
{
    apple_mt_spi_buf_ensure_capacity(buf, sizeof(val));
    stl_le_p(buf->data + buf->len, val);
    buf->len += sizeof(val);
}

static void apple_mt_spi_buf_push_crc16(AppleMTSPIBuffer *buf)
{
    g_assert_false(apple_mt_spi_buf_is_empty(buf));
    apple_mt_spi_buf_push_word(buf, crc16(0, buf->data, buf->len));
}

static void apple_mt_spi_buf_append(AppleMTSPIBuffer *buf,
                                    AppleMTSPIBuffer *other_buf)
{
    if (!apple_mt_spi_buf_is_empty(other_buf)) {
        apple_mt_spi_buf_ensure_capacity(buf, other_buf->len);
        memcpy(buf->data + buf->len, other_buf->data, other_buf->len);
        buf->len += other_buf->len;
    }
    apple_mt_spi_buf_free(other_buf);
}

static uint8_t apple_mt_spi_buf_pop(AppleMTSPIBuffer *buf)
{
    uint8_t ret;

    if (apple_mt_spi_buf_is_empty(buf)) {
        return 0;
    }

    g_assert_nonnull(buf->data);
    g_assert_cmphex(buf->len, >, buf->read_pos);

    ret = buf->data[buf->read_pos];

    if (apple_mt_spi_buf_read_pos_at_end(buf) ||
        apple_mt_spi_buf_is_empty(buf)) {
        apple_mt_spi_buf_free(buf);
    } else {
        buf->read_pos += 1;
    }

    return ret;
}

static inline uint8_t apple_mt_spi_buf_read_byte(const AppleMTSPIBuffer *buf,
                                                 size_t off)
{
    g_assert_nonnull(buf->data);
    g_assert_cmphex(off, <, buf->len);
    return buf->data[off];
}

static inline uint16_t apple_mt_spi_buf_read_word(const AppleMTSPIBuffer *buf,
                                                  size_t off)
{
    g_assert_nonnull(buf->data);
    g_assert_cmphex(off + sizeof(uint16_t), <, buf->len);
    return lduw_be_p(buf->data + off);
}

static inline uint32_t apple_mt_spi_buf_read_dword(const AppleMTSPIBuffer *buf,
                                                   size_t off)
{
    g_assert_nonnull(buf->data);
    g_assert_cmphex(off + sizeof(uint32_t), <, buf->len);
    return apple_mt_spi_buf_read_word(buf, off) |
           (apple_mt_spi_buf_read_word(buf, off + sizeof(uint16_t)) << 16);
}

static void apple_mt_spi_reset_unlocked(AppleMTSPIState *s, ResetType type)
{
    AppleMTSPILLPacket *packet;
    AppleMTSPILLPacket *packet_next;

    qemu_irq_raise(s->irq);

    timer_del(s->timer);
    timer_del(s->end_timer);

    s->btn_state = 0;
    s->prev_btn_state = 0;
    s->prev_x = 0;
    s->prev_y = 0;
    s->x = 0;
    s->y = 0;
    s->prev_ts = 0;
    s->frame = 0;

    apple_mt_spi_buf_free(&s->tx);
    apple_mt_spi_buf_free(&s->rx);
    apple_mt_spi_buf_free(&s->pending_hbpp);

    QTAILQ_FOREACH_SAFE (packet, &s->pending_fw, next, packet_next) {
        QTAILQ_REMOVE(&s->pending_fw, packet, next);
        apple_mt_spi_buf_free(&packet->buf);
        g_free(packet);
    }
}

static void apple_mt_spi_reset_hold(Object *obj, ResetType type)
{
    AppleMTSPIState *s;

    s = APPLE_MT_SPI(obj);

    QEMU_LOCK_GUARD(&s->lock);

    apple_mt_spi_reset_unlocked(s, type);
}

static void apple_mt_spi_push_pending_hbpp_word(AppleMTSPIState *s,
                                                uint16_t val)
{
    apple_mt_spi_buf_push_word(&s->pending_hbpp, val);
    qemu_irq_lower(s->irq);
}

static void apple_mt_spi_push_pending_hbpp_dword(AppleMTSPIState *s,
                                                 uint32_t val)
{
    apple_mt_spi_buf_push_dword(&s->pending_hbpp, val);
    qemu_irq_lower(s->irq);
}

static inline uint16_t apple_mt_spi_hbpp_packet_hdr_len(uint8_t val)
{
    switch (val) {
    case HBPP_PACKET_RESET:
        return 0x4;
    case HBPP_PACKET_NOP:
        return 0x2;
    case HBPP_PACKET_INT_ACK:
        return 0x2;
    case HBPP_PACKET_MEM_READ:
        return 0x8;
    case HBPP_PACKET_MEM_RMW:
        return 0x10;
    case HBPP_PACKET_REQ_CAL:
        return 0x2;
    case HBPP_PACKET_DATA:
        return 0xA;
    default:
        warn_report("Unknown HBPP packet type 0x%X", val);
        return 0x2;
    }
}

static void apple_mt_spi_handle_hbpp_data(AppleMTSPIState *s)
{
    uint16_t payload_len;
    uint16_t new_rx_capacity;

    if (!apple_mt_spi_buf_is_full(&s->rx)) {
        return;
    }

    payload_len = apple_mt_spi_buf_read_word(&s->rx, 2) * sizeof(uint32_t);
    new_rx_capacity = apple_mt_spi_hbpp_packet_hdr_len(HBPP_PACKET_DATA) +
                      payload_len + sizeof(uint32_t);

    if (s->rx.capacity == new_rx_capacity) {
        apple_mt_spi_push_pending_hbpp_word(s, HBPP_PACKET_ACK_DATA);
    } else {
        apple_mt_spi_buf_set_capacity(&s->rx, new_rx_capacity);
    }
}

static void apple_mt_spi_handle_hbpp_mem_rmw(AppleMTSPIState *s)
{
    if (apple_mt_spi_buf_is_full(&s->rx)) {
        apple_mt_spi_push_pending_hbpp_word(s, HBPP_PACKET_ACK_WR_REQ);
    }
}

static void apple_mt_spi_handle_hbpp(AppleMTSPIState *s)
{
    uint8_t packet_type;

    packet_type = apple_mt_spi_buf_read_byte(&s->rx, 0);

    if (apple_mt_spi_buf_pos_at_start(&s->rx)) {
        apple_mt_spi_buf_set_capacity(
            &s->rx, apple_mt_spi_hbpp_packet_hdr_len(packet_type));
    }

    switch (packet_type) {
    case HBPP_PACKET_RESET:
        if (apple_mt_spi_buf_is_full(&s->rx)) {
            apple_mt_spi_reset_unlocked(s, RESET_TYPE_COLD);
            apple_mt_spi_push_pending_hbpp_word(s, HBPP_PACKET_REQ_BOOT);
        }
        break;
    case HBPP_PACKET_NOP:
        if (apple_mt_spi_buf_pos_at_start(&s->rx) &&
            apple_mt_spi_buf_is_empty(&s->tx)) {
            apple_mt_spi_buf_push_word(&s->tx, HBPP_PACKET_ACK_NOP);
        }
        break;
    case HBPP_PACKET_INT_ACK:
        if (apple_mt_spi_buf_pos_at_start(&s->rx)) {
            apple_mt_spi_buf_append(&s->tx, &s->pending_hbpp);
        }
        break;
    case HBPP_PACKET_MEM_READ:
        if (apple_mt_spi_buf_pos_at_start(&s->rx)) {
            apple_mt_spi_push_pending_hbpp_word(s, HBPP_PACKET_ACK_RD_REQ);
            apple_mt_spi_push_pending_hbpp_dword(s, 0x00000000); // value
            apple_mt_spi_push_pending_hbpp_word(s, 0x0000); // crc16 of value
        }
        break;
    case HBPP_PACKET_MEM_RMW:
        apple_mt_spi_handle_hbpp_mem_rmw(s);
        break;
    case HBPP_PACKET_REQ_CAL:
        if (apple_mt_spi_buf_pos_at_start(&s->rx)) {
            apple_mt_spi_push_pending_hbpp_word(s, HBPP_PACKET_CAL_DONE);
        }
        break;
    case HBPP_PACKET_DATA:
        apple_mt_spi_handle_hbpp_data(s);
        break;
    default:
        error_report("%s: Unknown packet type 0x%02X", __func__, packet_type);
        break;
    }
}

static void apple_mt_spi_push_preamble(AppleMTSPIBuffer *buf)
{
    apple_mt_spi_buf_push_dword(buf, LL_PACKET_PREAMBLE);
}

static void apple_mt_spi_push_ll_hdr(AppleMTSPIBuffer *buf, uint8_t type,
                                     uint8_t interface, uint16_t payload_off,
                                     uint16_t payload_remaining,
                                     uint16_t payload_length)
{
    apple_mt_spi_buf_push_byte(buf, type);
    apple_mt_spi_buf_push_byte(buf, interface);
    apple_mt_spi_buf_push_word(buf, payload_off);
    apple_mt_spi_buf_push_word(buf, payload_remaining);
    apple_mt_spi_buf_push_word(buf, payload_length);
}

static void apple_mt_spi_pad_ll_packet(AppleMTSPIBuffer *buf)
{
    apple_mt_spi_buf_set_len(
        buf, 0, LL_PACKET_LEN - sizeof(uint32_t) - sizeof(uint16_t));
}

static void apple_mt_spi_push_no_data(AppleMTSPIBuffer *buf)
{
    apple_mt_spi_push_ll_hdr(buf, LL_PACKET_NO_DATA, 0, 0, 0, 0);
    apple_mt_spi_pad_ll_packet(buf);
    apple_mt_spi_buf_push_crc16(buf);
}

static uint8_t apple_mt_spi_ll_read_payload_byte(AppleMTSPIBuffer *buf,
                                                 size_t off)
{
    g_assert_false(apple_mt_spi_buf_is_empty(buf));
    g_assert_cmphex(sizeof(uint32_t) + sizeof(uint64_t) + off + sizeof(uint8_t),
                    <=, buf->len);
    return buf->data[sizeof(uint32_t) + sizeof(uint64_t) + off];
}

static uint16_t apple_mt_spi_ll_read_payload_word(AppleMTSPIBuffer *buf,
                                                  size_t off)
{
    g_assert_false(apple_mt_spi_buf_is_empty(buf));
    g_assert_cmphex(sizeof(uint32_t) + sizeof(uint64_t) + off +
                        sizeof(uint16_t),
                    <=, buf->len);
    return lduw_le_p(buf->data + sizeof(uint32_t) + sizeof(uint64_t) + off);
}

static void apple_mt_spi_push_hid_hdr(AppleMTSPIBuffer *buf, uint8_t type,
                                      uint8_t report_id, uint8_t packet_status,
                                      uint8_t frame_number,
                                      uint16_t length_requested,
                                      uint16_t payload_length)
{
    apple_mt_spi_buf_push_byte(buf, type);
    apple_mt_spi_buf_push_byte(buf, report_id);
    apple_mt_spi_buf_push_byte(buf, packet_status);
    apple_mt_spi_buf_push_byte(buf, frame_number);
    apple_mt_spi_buf_push_word(buf, length_requested);
    apple_mt_spi_buf_push_word(buf, payload_length);
}

static void apple_mt_spi_push_report_hdr(AppleMTSPIBuffer *buf, uint8_t type,
                                         uint8_t report_id,
                                         uint8_t packet_status,
                                         uint8_t frame_number,
                                         uint16_t payload_length)
{
    apple_mt_spi_push_hid_hdr(buf, type, report_id, packet_status, frame_number,
                              0, payload_length + sizeof(report_id));
    apple_mt_spi_buf_push_byte(buf, report_id);
}

static void apple_mt_spi_push_report_byte(AppleMTSPIBuffer *buf, uint8_t type,
                                          uint8_t report_id,
                                          uint8_t packet_status,
                                          uint8_t frame_number, uint8_t val)
{
    apple_mt_spi_push_report_hdr(buf, type, report_id, packet_status,
                                 frame_number, sizeof(val));
    apple_mt_spi_buf_push_byte(buf, val);
}

static void apple_mt_spi_handle_get_feature(AppleMTSPIState *s)
{
    AppleMTSPILLPacket *packet;
    uint8_t report_id;
    uint8_t frame_number;

    report_id = apple_mt_spi_ll_read_payload_byte(&s->rx, sizeof(uint8_t));
    frame_number =
        apple_mt_spi_ll_read_payload_byte(&s->rx, sizeof(uint8_t) * 3);

    packet = g_new0(AppleMTSPILLPacket, 1);
    packet->type = LL_PACKET_CONTROL;

    switch (report_id) {
    case HID_REPORT_FAMILY_ID:
        apple_mt_spi_push_report_byte(
            &packet->buf, HID_CONTROL_PACKET_SET_OUTPUT_REPORT, report_id,
            HID_PACKET_STATUS_SUCCESS, frame_number, MT_FAMILY_ID);
        break;
    case HID_REPORT_BASIC_DEVICE_INFO:
        apple_mt_spi_push_report_hdr(
            &packet->buf, HID_CONTROL_PACKET_SET_OUTPUT_REPORT, report_id,
            HID_PACKET_STATUS_SUCCESS, frame_number, 5);
        apple_mt_spi_buf_push_byte(&packet->buf, MT_LITTLE_ENDIAN);
        apple_mt_spi_buf_push_byte(&packet->buf, MT_ROWS);
        apple_mt_spi_buf_push_byte(&packet->buf, MT_COLUMNS);
        apple_mt_spi_buf_push_word(&packet->buf, MT_BCD_VER);
        break;
    case HID_REPORT_SENSOR_SURFACE_DESC:
        apple_mt_spi_push_report_hdr(
            &packet->buf, HID_CONTROL_PACKET_SET_OUTPUT_REPORT, report_id,
            HID_PACKET_STATUS_SUCCESS, frame_number, 16);
        apple_mt_spi_buf_push_dword(&packet->buf, MT_SENSOR_SURFACE_WIDTH);
        apple_mt_spi_buf_push_dword(&packet->buf, MT_SENSOR_SURFACE_HEIGHT);
        // these values might need to be different, especially considering the
        // values/stuff inside HID_REPORT_SENSOR_REGION_DESC.
        apple_mt_spi_buf_push_word(&packet->buf, 0);
        apple_mt_spi_buf_push_word(&packet->buf, 0);
        apple_mt_spi_buf_push_word(&packet->buf, MT_SENSOR_SURFACE_WIDTH);
        apple_mt_spi_buf_push_word(&packet->buf, MT_SENSOR_SURFACE_HEIGHT);
        break;
    case HID_REPORT_SENSOR_REGION_PARAM:
        apple_mt_spi_push_report_hdr(
            &packet->buf, HID_CONTROL_PACKET_SET_OUTPUT_REPORT, report_id,
            HID_PACKET_STATUS_SUCCESS, frame_number, 6);
        apple_mt_spi_buf_push_word(&packet->buf, 0x0);
        apple_mt_spi_buf_push_word(&packet->buf, 0x7);
        apple_mt_spi_buf_push_word(&packet->buf, 0x200);
        break;
    case HID_REPORT_SENSOR_REGION_DESC:
        apple_mt_spi_push_report_hdr(
            &packet->buf, HID_CONTROL_PACKET_SET_OUTPUT_REPORT, report_id,
            HID_PACKET_STATUS_SUCCESS, frame_number, 22); // 1 + 7*3
        apple_mt_spi_buf_push_byte(&packet->buf, 3); // region count

        apple_mt_spi_buf_push_byte(&packet->buf, 1); // type Multitouch
        apple_mt_spi_buf_push_byte(&packet->buf, 0); // start_row
        apple_mt_spi_buf_push_byte(&packet->buf, 30); // rows
        apple_mt_spi_buf_push_byte(&packet->buf, 1); // row_skip
        apple_mt_spi_buf_push_byte(&packet->buf, 0); // start_col
        apple_mt_spi_buf_push_byte(&packet->buf, 14); // cols
        apple_mt_spi_buf_push_byte(&packet->buf, 0); // hardware_coloffset

        apple_mt_spi_buf_push_byte(&packet->buf, 8); // type CommonMode
        apple_mt_spi_buf_push_byte(&packet->buf, 0); // start_row
        apple_mt_spi_buf_push_byte(&packet->buf, 30); // rows
        apple_mt_spi_buf_push_byte(&packet->buf, 1); // row_skip
        apple_mt_spi_buf_push_byte(&packet->buf, 14); // start_col
        apple_mt_spi_buf_push_byte(&packet->buf, 1); // cols
        apple_mt_spi_buf_push_byte(&packet->buf, 0); // hardware_coloffset

        apple_mt_spi_buf_push_byte(&packet->buf, 11); // type Unknown
        apple_mt_spi_buf_push_byte(&packet->buf, 30); // maybe rows?
        apple_mt_spi_buf_push_byte(&packet->buf, 1); // maybe row_skip?
        apple_mt_spi_buf_push_byte(&packet->buf, 1); // unknown
        apple_mt_spi_buf_push_byte(&packet->buf, 0); // unknown
        // maybe ignored? offsets 0x14/0x15 > size 0x14
        apple_mt_spi_buf_push_byte(&packet->buf, 15);
        apple_mt_spi_buf_push_byte(&packet->buf, 0);
        break;
    default:
        apple_mt_spi_push_report_byte(
            &packet->buf, HID_CONTROL_PACKET_SET_OUTPUT_REPORT, report_id,
            HID_PACKET_STATUS_SUCCESS, frame_number, 0);
        break;
    }
    apple_mt_spi_buf_push_crc16(&packet->buf);
    QTAILQ_INSERT_TAIL(&s->pending_fw, packet, next);
}

static void apple_mt_spi_handle_set_feature(AppleMTSPIState *s)
{
    AppleMTSPILLPacket *packet;
    uint8_t report_id;
    uint8_t frame_number;

    report_id = apple_mt_spi_ll_read_payload_byte(&s->rx, sizeof(uint8_t));
    frame_number =
        apple_mt_spi_ll_read_payload_byte(&s->rx, sizeof(uint8_t) * 3);

    packet = g_new0(AppleMTSPILLPacket, 1);
    packet->type = LL_PACKET_CONTROL;
    apple_mt_spi_push_hid_hdr(&packet->buf,
                              HID_CONTROL_PACKET_SET_OUTPUT_REPORT, report_id,
                              HID_PACKET_STATUS_SUCCESS, frame_number, 0, 0);
    apple_mt_spi_buf_push_crc16(&packet->buf);
    QTAILQ_INSERT_TAIL(&s->pending_fw, packet, next);
}

static void apple_mt_spi_handle_control(AppleMTSPIState *s)
{
    uint8_t type = apple_mt_spi_ll_read_payload_byte(&s->rx, 0);

    switch (type) {
    case HID_CONTROL_PACKET_GET_FEATURE_REPORT:
        apple_mt_spi_handle_get_feature(s);
        break;
    case HID_CONTROL_PACKET_SET_FEATURE_REPORT:
        apple_mt_spi_handle_set_feature(s);
        break;
    default:
        warn_report("Unknown HID packet type 0x%X", type);
        break;
    }
}

static void apple_mt_spi_handle_fw_packet(AppleMTSPIState *s)
{
    uint8_t packet_type;
    AppleMTSPIBuffer buf = { 0 };
    AppleMTSPILLPacket *packet;

    if (apple_mt_spi_buf_get_pos(&s->rx) == sizeof(uint32_t)) {
        apple_mt_spi_buf_set_capacity(&s->rx, LL_PACKET_LEN);

        if (QTAILQ_EMPTY(&s->pending_fw)) {
            apple_mt_spi_push_no_data(&buf);
        } else {
            packet = QTAILQ_FIRST(&s->pending_fw);
            g_assert_nonnull(packet);
            apple_mt_spi_push_ll_hdr(&buf, packet->type, 0, 0, 0,
                                     packet->buf.len);
            apple_mt_spi_buf_append(&buf, &packet->buf);
            apple_mt_spi_pad_ll_packet(&buf);
            apple_mt_spi_buf_push_crc16(&buf);
            QTAILQ_REMOVE(&s->pending_fw, packet, next);
            g_free(packet);
            packet = NULL;
        }

        apple_mt_spi_buf_append(&s->tx, &buf);
    }

    if (!apple_mt_spi_buf_is_full(&s->rx)) {
        return;
    }

    packet_type = apple_mt_spi_buf_read_byte(&s->rx, sizeof(uint32_t));

    switch (packet_type) {
    case LL_PACKET_NO_DATA:
        break;
    case LL_PACKET_CONTROL:
        apple_mt_spi_handle_control(s);
        break;
    default:
        warn_report("%s: Unknown LL packet type 0x%X", __func__, packet_type);
        break;
    }
}

static void apple_mt_spi_handle_fw(AppleMTSPIState *s)
{
    uint8_t packet_type;

    if (apple_mt_spi_buf_pos_at_start(&s->rx)) {
        apple_mt_spi_buf_set_capacity(&s->rx, sizeof(uint32_t) * 2);
        apple_mt_spi_push_preamble(&s->tx);
    }

    if (apple_mt_spi_buf_get_pos(&s->rx) < sizeof(uint32_t)) {
        return;
    }

    packet_type = apple_mt_spi_buf_read_byte(&s->rx, sizeof(uint32_t));

    switch (packet_type) {
    case LL_PACKET_ERROR & 0xFF:
    case LL_PACKET_ACK & 0xFF:
    case LL_PACKET_NAK & 0xFF:
    case LL_PACKET_BUSY & 0xFF:
        if (apple_mt_spi_buf_get_pos(&s->rx) == sizeof(uint32_t)) {
            apple_mt_spi_buf_push_dword(&s->tx, LL_PACKET_ACK);
        }
        break;
    default:
        apple_mt_spi_handle_fw_packet(s);
        break;
    }
}

static uint32_t apple_mt_spi_transfer(SSIPeripheral *dev, uint32_t val)
{
    AppleMTSPIState *s;
    uint8_t ret;

    s = container_of(dev, AppleMTSPIState, parent_obj);

    QEMU_LOCK_GUARD(&s->lock);

    apple_mt_spi_buf_push_byte(&s->rx, (uint8_t)val);

    if (apple_mt_spi_buf_read_byte(&s->rx, 0) == (LL_PACKET_PREAMBLE & 0xFF)) {
        apple_mt_spi_handle_fw(s);
    } else {
        apple_mt_spi_handle_hbpp(s);
    }

    if (apple_mt_spi_buf_is_full(&s->rx)) {
        apple_mt_spi_buf_free(&s->rx);
    }

    ret = apple_mt_spi_buf_pop(&s->tx);

    if (apple_mt_spi_buf_is_empty(&s->pending_hbpp) &&
        QTAILQ_EMPTY(&s->pending_fw)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }

    return ret;
}

static void apple_mt_spi_send_path_update(AppleMTSPIState *s, uint64_t ts,
                                          uint8_t path_stage)
{
    AppleMTSPILLPacket *packet;
    uint64_t ts_delta;
    int32_t x_delta;
    int32_t y_delta;

    packet = g_new0(AppleMTSPILLPacket, 1);

    ts_delta = ts - s->prev_ts;
    ts_delta = MAX(ts_delta, 1); // Prevent div-by-zero
    s->prev_ts = ts;

    x_delta = s->x - s->prev_x;
    y_delta = s->y - s->prev_y;

    packet->type = LL_PACKET_LOSSLESS_OUTPUT;
    apple_mt_spi_push_report_hdr(&packet->buf, HID_TRANSFER_PACKET_OUTPUT,
                                 HID_REPORT_BINARY_PATH_OR_IMAGE,
                                 HID_PACKET_STATUS_SUCCESS, s->frame, 27 + 20);
    apple_mt_spi_buf_push_byte(&packet->buf, s->frame);
    apple_mt_spi_buf_push_byte(&packet->buf, 28); // Header Len
    apple_mt_spi_buf_push_byte(&packet->buf, 0);
    apple_mt_spi_buf_push_dword(&packet->buf, ts / SCALE_MS);
    apple_mt_spi_buf_push_byte(&packet->buf, 0);
    apple_mt_spi_buf_push_byte(&packet->buf, 0);
    apple_mt_spi_buf_push_byte(&packet->buf, 0);
    apple_mt_spi_buf_push_byte(&packet->buf, 0);
    apple_mt_spi_buf_push_word(&packet->buf, 0);
    apple_mt_spi_buf_push_word(&packet->buf, 0); // Image Len
    apple_mt_spi_buf_push_byte(&packet->buf, 1); // Path Count
    apple_mt_spi_buf_push_byte(&packet->buf, 20); // Path Len
    apple_mt_spi_buf_push_word(&packet->buf, 0);
    apple_mt_spi_buf_push_word(&packet->buf, 0);
    apple_mt_spi_buf_push_word(&packet->buf, 0);
    apple_mt_spi_buf_push_byte(&packet->buf, 0);
    apple_mt_spi_buf_push_byte(&packet->buf, 0);
    apple_mt_spi_buf_push_byte(&packet->buf, 0);
    apple_mt_spi_buf_push_byte(&packet->buf, 0);

    // Path 0
    apple_mt_spi_buf_push_byte(&packet->buf, 1); // Path ID
    apple_mt_spi_buf_push_byte(&packet->buf, path_stage);
    apple_mt_spi_buf_push_byte(&packet->buf, 1); // Finger ID
    apple_mt_spi_buf_push_byte(&packet->buf, 1); // Hand ID
    apple_mt_spi_buf_push_word(&packet->buf, s->x);
    apple_mt_spi_buf_push_word(&packet->buf, s->y);
    apple_mt_spi_buf_push_word(&packet->buf, ABS(x_delta) / ts_delta * 1000);
    apple_mt_spi_buf_push_word(&packet->buf, ABS(y_delta) / ts_delta * 1000);
    apple_mt_spi_buf_push_word(&packet->buf, 660); // rad0
    apple_mt_spi_buf_push_word(&packet->buf,
                               580); // rad1
    // no freaking idea if this is even remotely correct.
    // int angle = 0;
    // double deltaX = s->x - s->prev_x;
    // double deltaY = s->y - s->prev_y;
    // double rad = atan2(deltaY, deltaX);
    // double deg = rad * (180 / PI);
    // angle = deg;
    // angle = lround(deg);
    // angle = 19317;
    // angle = 90;
    apple_mt_spi_buf_push_word(&packet->buf, 19317); // angle/orientation
    apple_mt_spi_buf_push_word(&packet->buf,
                               100); // rad multiplier (maybe force?)
    // let iOS calculate the contact density by itself
    // rad0 = max(maximum_radii, rad0)
    // rad1 = max(maximum_radii, rad1)
    // sqr = sqrt(rad0 * rad1)
    // sqr = max(maximum_radii, sqr)
    // contactDensityByRadii = (mult * 400) / (sqr - minimum_radii)
    // apple_mt_spi_buf_push_word(&packet->buf, 150); // contact density

    apple_mt_spi_buf_push_crc16(&packet->buf);

    if (s->frame < 0xFF) {
        ++s->frame;
    } else {
        s->frame = 0;
    }

    QTAILQ_INSERT_TAIL(&s->pending_fw, packet, next);
    qemu_irq_lower(s->irq);
}

typedef struct {
    AppleMTSPIState *s;
    uint64_t ts;
    uint8_t path_stage;
} AppleMTSPITouchUpdate;

static AppleMTSPITouchUpdate *apple_mt_spi_new_touch_update(AppleMTSPIState *s,
                                                            uint8_t path_stage)
{
    AppleMTSPITouchUpdate *update = g_new(AppleMTSPITouchUpdate, 1);
    update->s = s;
    update->ts = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    update->path_stage = path_stage;
    return update;
}

static void apple_mt_spi_send_touch_update_bh(void *opaque)
{
    AppleMTSPITouchUpdate *update = opaque;

    QEMU_LOCK_GUARD(&update->s->lock);

    apple_mt_spi_send_path_update(update->s, update->ts, update->path_stage);

    g_free(opaque);
}

static void apple_mt_spi_schedule_touch_update(AppleMTSPIState *s,
                                               uint8_t path_stage)
{
    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                            apple_mt_spi_send_touch_update_bh,
                            apple_mt_spi_new_touch_update(s, path_stage));
}

static void apple_mt_spi_timer_tick(void *opaque)
{
    AppleMTSPIState *s = opaque;

    QEMU_LOCK_GUARD(&s->lock);

    if (s->prev_x != s->x || s->prev_y != s->y) {
        apple_mt_spi_schedule_touch_update(s, PATH_STAGE_TOUCHING);
    }

    if (s->btn_state & MOUSE_EVENT_LBUTTON) {
        timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                NANOSECONDS_PER_SECOND / 10);
    }
}

static void apple_mt_spi_end_timer_tick(void *opaque)
{
    AppleMTSPIState *s = opaque;

    QEMU_LOCK_GUARD(&s->lock);

    apple_mt_spi_schedule_touch_update(s, PATH_STAGE_OUT_OF_RANGE);

    s->prev_ts = 0;
    s->prev_x = 0;
    s->prev_y = 0;
}

static void apple_mt_spi_mouse_event(void *opaque, int dx, int dy, int dz,
                                     int buttons_state)
{
    AppleMTSPIState *s = opaque;

    QEMU_LOCK_GUARD(&s->lock);

    s->prev_x = s->x;
    s->prev_y = s->y;
    s->x = qemu_input_scale_axis(dx, INPUT_EVENT_ABS_MIN, INPUT_EVENT_ABS_MAX,
                                 0, MT_SENSOR_SURFACE_WIDTH);
    s->y =
        qemu_input_scale_axis(INPUT_EVENT_ABS_MAX - dy, INPUT_EVENT_ABS_MIN,
                              INPUT_EVENT_ABS_MAX, 0, MT_SENSOR_SURFACE_HEIGHT);
    // Hardcoded calibration on y-axis.
    // Tested accuracy for display_height 1792 is +/- 1 pixel.
    // it might not be perfect, also there might be some calibration needed for
    // "x".
    // s->y -= qemu_input_scale_axis(16, 0, 1792, 0,
    // MT_SENSOR_SURFACE_HEIGHT);
    // fprintf(stderr, "%s: display_height: %u ; display_width: %u\n", __func__,
    //         s->display_height, s->display_width);
    s->y -= qemu_input_scale_axis(16, 0, s->display_height, 0,
                                  MT_SENSOR_SURFACE_HEIGHT);
    s->prev_btn_state = s->btn_state;
    s->btn_state = buttons_state;

    if ((s->prev_btn_state & MOUSE_EVENT_LBUTTON) == 0 &&
        (s->btn_state & MOUSE_EVENT_LBUTTON) != 0) {
        apple_mt_spi_schedule_touch_update(s, PATH_STAGE_MAKE_TOUCH);

        timer_del(s->end_timer);
        timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                NANOSECONDS_PER_SECOND / 10);
    } else if ((s->prev_btn_state & MOUSE_EVENT_LBUTTON) != 0 &&
               (s->btn_state & MOUSE_EVENT_LBUTTON) == 0) {
        apple_mt_spi_schedule_touch_update(s, PATH_STAGE_BREAK_TOUCH);

        timer_del(s->timer);
        timer_mod(s->end_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    NANOSECONDS_PER_SECOND / 10);
    }
}
static void apple_mt_spi_realize(SSIPeripheral *dev, Error **errp)
{
    AppleMTSPIState *s;
    QEMUPutMouseEntry *entry;

    s = container_of(dev, AppleMTSPIState, parent_obj);

    entry = qemu_add_mouse_event_handler(apple_mt_spi_mouse_event, s, 1,
                                         "Apple Multitouch HID SPI");
    qemu_activate_mouse_event_handler(entry);
}

static const Property apple_mt_spi_props[] = {
    DEFINE_PROP_UINT32("display_width", AppleMTSPIState, display_width, 0),
    DEFINE_PROP_UINT32("display_height", AppleMTSPIState, display_height, 0),
};

static const VMStateDescription vmstate_apple_mt_spi_buffer = {
    .name = "AppleMTSPIBuffer",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32(capacity, AppleMTSPIBuffer),
            VMSTATE_VBUFFER_ALLOC_UINT32(data, AppleMTSPIBuffer, 0, NULL,
                                         capacity),
            VMSTATE_UINT32(len, AppleMTSPIBuffer),
            VMSTATE_UINT32(read_pos, AppleMTSPIBuffer),
            VMSTATE_END_OF_LIST(),
        },
};

static const VMStateDescription vmstate_apple_mt_spi_ll_packet = {
    .name = "AppleMTSPILLPacket",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_STRUCT(buf, AppleMTSPILLPacket, 0,
                           vmstate_apple_mt_spi_buffer, AppleMTSPIBuffer),
            VMSTATE_UINT8(type, AppleMTSPILLPacket),
            VMSTATE_END_OF_LIST(),
        },
};

static const VMStateDescription vmstate_apple_mt_spi = {
    .name = "AppleMTSPIState",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_SSI_PERIPHERAL(parent_obj, AppleMTSPIState),
            VMSTATE_STRUCT(tx, AppleMTSPIState, 0, vmstate_apple_mt_spi_buffer,
                           AppleMTSPIBuffer),
            VMSTATE_STRUCT(rx, AppleMTSPIState, 0, vmstate_apple_mt_spi_buffer,
                           AppleMTSPIBuffer),
            VMSTATE_STRUCT(pending_hbpp, AppleMTSPIState, 0,
                           vmstate_apple_mt_spi_buffer, AppleMTSPIBuffer),
            VMSTATE_QTAILQ_V(pending_fw, AppleMTSPIState, 0,
                             vmstate_apple_mt_spi_ll_packet, AppleMTSPILLPacket,
                             next),
            VMSTATE_UINT8(frame, AppleMTSPIState),
            VMSTATE_TIMER_PTR(timer, AppleMTSPIState),
            VMSTATE_TIMER_PTR(end_timer, AppleMTSPIState),
            VMSTATE_INT16(x, AppleMTSPIState),
            VMSTATE_INT16(y, AppleMTSPIState),
            VMSTATE_INT16(prev_x, AppleMTSPIState),
            VMSTATE_INT16(prev_y, AppleMTSPIState),
            VMSTATE_UINT64(prev_ts, AppleMTSPIState),
            VMSTATE_INT32(btn_state, AppleMTSPIState),
            VMSTATE_INT32(prev_btn_state, AppleMTSPIState),
            VMSTATE_UINT32(display_width, AppleMTSPIState),
            VMSTATE_UINT32(display_height, AppleMTSPIState),
            VMSTATE_END_OF_LIST(),
        },
};

static void apple_mt_spi_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    rc->phases.hold = apple_mt_spi_reset_hold;

    dc->user_creatable = false;
    dc->vmsd = &vmstate_apple_mt_spi;
    device_class_set_props(dc, apple_mt_spi_props);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    k->realize = apple_mt_spi_realize;
    k->transfer = apple_mt_spi_transfer;
}

static void apple_mt_instance_init(Object *obj)
{
    AppleMTSPIState *s;

    s = APPLE_MT_SPI(obj);

    qdev_init_gpio_out_named(DEVICE(s), &s->irq, APPLE_MT_SPI_IRQ, 1);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, apple_mt_spi_timer_tick, s);
    s->end_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, apple_mt_spi_end_timer_tick, s);

    QTAILQ_INIT(&s->pending_fw);

    qemu_mutex_init(&s->lock);
}

static const TypeInfo apple_mt_spi_type_info = {
    .name = TYPE_APPLE_MT_SPI,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(AppleMTSPIState),
    .instance_init = apple_mt_instance_init,
    .class_init = apple_mt_spi_class_init,
};

static void apple_mt_spi_register_types(void)
{
    type_register_static(&apple_mt_spi_type_info);
}

type_init(apple_mt_spi_register_types);
