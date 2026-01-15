/*
 * Apple SMC.
 *
 * Copyright (c) 2023-2026 Visual Ehrmanntraut (VisualEhrmanntraut).
 * Copyright (c) 2023-2026 Christian Inci (chris-pcguy).
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
#include "hw/misc/apple-silicon/a7iop/rtkit.h"
#include "hw/misc/apple-silicon/smc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/memalign.h"
#include "qemu/queue.h"
#include "system/runstate.h"

#if 0
#include "qemu/log.h"
#define SMC_LOG_MSG(ep, msg)       \
    qemu_log_mask(LOG_GUEST_ERROR, \
                  "SMC: message: ep=%u msg=0x" HWADDR_FMT_plx "\n", ep, msg)
#else
#define SMC_LOG_MSG(ep, msg) \
    do {                     \
    } while (0)
#endif

#define APPLE_SMC_MMIO_ASC (0)
#define APPLE_SMC_MMIO_SRAM (1)

typedef struct {
    uint8_t cmd;
    uint8_t tag_and_id;
    uint8_t length;
    uint8_t payload_length;
    uint32_t key;
} QEMU_PACKED KeyMessage;

struct AppleSMCClass {
    AppleRTKitClass parent_class;

    ResettablePhases parent_phases;
};

struct AppleSMCState {
    AppleRTKit parent_obj;

    MemoryRegion iomems[2];
    QTAILQ_HEAD(, SMCKey) keys;
    QTAILQ_HEAD(, SMCKeyData) key_data;
    uint8_t *sram;
    uint32_t sram_size;
    bool is_booted;
};

SMCKey *apple_smc_get_key(AppleSMCState *s, uint32_t key)
{
    SMCKey *key_entry;

    QTAILQ_FOREACH (key_entry, &s->keys, next) {
        if (key_entry->key == key) {
            return key_entry;
        }
    }

    return NULL;
}

SMCKeyData *apple_smc_get_key_data(AppleSMCState *s, uint32_t key)
{
    SMCKeyData *data_entry;

    QTAILQ_FOREACH (data_entry, &s->key_data, next) {
        if (data_entry->key == key) {
            return data_entry;
        }
    }

    return NULL;
}

static SMCKey *apple_smc_new_key(uint32_t key, uint8_t size, SMCKeyType type,
                                 SMCKeyAttribute attr, const void *data,
                                 SMCKeyData **out_data_entry)
{
    SMCKey *key_entry;
    SMCKeyData *data_entry;

    g_assert_nonnull(out_data_entry);

    key_entry = g_new0(SMCKey, 1);
    data_entry = g_new0(SMCKeyData, 1);

    key_entry->key = key;
    key_entry->info.size = size;
    key_entry->info.type = cpu_to_be32(type);
    key_entry->info.attr = attr;
    data_entry->key = key;
    data_entry->size = size;

    if (data == NULL) {
        data_entry->data = g_malloc0(size);
    } else {
        data_entry->data = g_malloc(size);
        memcpy(data_entry->data, data, size);
    }

    *out_data_entry = data_entry;

    return key_entry;
}

static void apple_smc_insert_key(AppleSMCState *s, SMCKey *key_entry,
                                 SMCKeyData *data_entry)
{
    if (apple_smc_get_key(s, key_entry->key) != NULL) {
        error_setg(&error_fatal, "duplicate SMC key `%c%c%c%c`",
                   SMC_KEY_FORMAT(key_entry->key));
        return;
    }

    QTAILQ_INSERT_TAIL(&s->keys, key_entry, next);
    QTAILQ_INSERT_TAIL(&s->key_data, data_entry, next);
}

void apple_smc_add_key(AppleSMCState *s, uint32_t key, uint8_t size,
                       SMCKeyType type, SMCKeyAttribute attr, const void *data)
{
    SMCKey *key_entry;
    SMCKeyData *data_entry;

    g_assert_false(attr & SMC_ATTR_FUNC);

    key_entry = apple_smc_new_key(key, size, type, attr, data, &data_entry);

    apple_smc_insert_key(s, key_entry, data_entry);
}

void apple_smc_add_key_func(AppleSMCState *s, uint32_t key, uint8_t size,
                            SMCKeyType type, SMCKeyAttribute attr, void *opaque,
                            SMCKeyFunc *reader, SMCKeyFunc *writer)
{
    SMCKey *key_entry;
    SMCKeyData *data_entry;

    g_assert_false(attr & (SMC_ATTR_FUNC | SMC_ATTR_RW));

    attr |= SMC_ATTR_FUNC;
    if (reader != NULL) {
        attr |= SMC_ATTR_R;
    }
    if (writer != NULL) {
        attr |= SMC_ATTR_W;
    }

    key_entry = apple_smc_new_key(key, size, type, attr, NULL, &data_entry);
    key_entry->opaque = opaque;
    key_entry->read = reader;
    key_entry->write = writer;

    apple_smc_insert_key(s, key_entry, data_entry);
}

static SMCResult apple_smc_key_read(const SMCKey *key_entry,
                                    const SMCKeyData *data_entry, uint32_t size,
                                    void *out)
{
    if (size < key_entry->info.size) {
        return SMC_RESULT_KEY_SIZE_MISMATCH;
    }
    memcpy(out, data_entry->data, key_entry->info.size);
    return SMC_RESULT_SUCCESS;
}

void apple_smc_send_hid_button(AppleSMCState *s, AppleSMCHIDButton button,
                               bool state)
{
    AppleRTKit *rtk;
    KeyResponse r = { 0 };

    if (!s->is_booted) {
        return;
    }

    rtk = APPLE_RTKIT(s);

    r.status = SMC_NOTIFICATION;
    r.response[0] = state;
    r.response[1] = button;
    r.response[2] = SMC_HID_EVENT_NOTIFY_BUTTON;
    r.response[3] = SMC_EVENT_HID_EVENT_NOTIFY;
    apple_rtkit_send_user_msg(rtk, kSMCKeyEndpoint, r.raw);
}

static SMCResult smc_key_count_read(SMCKey *key, SMCKeyData *data,
                                    const void *in, uint8_t in_length)
{
    AppleSMCState *s = key->opaque;
    SMCKey *cur;
    SMCKey *next;
    uint32_t key_count = 0;

    QTAILQ_FOREACH_SAFE (cur, &s->keys, next, next) {
        ++key_count;
    }

    stl_be_p(data->data, key_count);

    return SMC_RESULT_SUCCESS;
}

static SMCResult apple_smc_mbse_write(SMCKey *key, SMCKeyData *data,
                                      const void *in, uint8_t in_length)
{
    AppleSMCState *s = key->opaque;
    AppleRTKit *rtk;
    uint32_t value;
    KeyResponse r = { 0 };

    if (in == NULL || in_length != key->info.size) {
        return SMC_RESULT_BAD_ARGUMENT_ERROR;
    }

    rtk = APPLE_RTKIT(s);
    value = ldl_le_p(in);

    switch (value) {
    case 'susp':
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        return SMC_RESULT_SUCCESS;
    case 'rest':
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return SMC_RESULT_SUCCESS;
    case 'waka': // FIXME: Are we supposed to do anything here?
        return SMC_RESULT_ERROR;
    case 'slpa':
        qemu_system_suspend_request();
        return SMC_RESULT_SUCCESS;
    case 'panb': {
        r.status = SMC_NOTIFICATION;
        r.response[2] = SMC_SYSTEM_STATE_NOTIFY_SMC_PANIC_PROGRESS;
        r.response[3] = SMC_EVENT_SYSTEM_STATE_NOTIFY;
        apple_rtkit_send_user_msg(rtk, kSMCKeyEndpoint, r.raw);
        return SMC_RESULT_SUCCESS;
    }
    case 'pane': {
        r.status = SMC_NOTIFICATION;
        r.response[2] = SMC_SYSTEM_STATE_NOTIFY_SMC_PANIC_DONE;
        r.response[3] = SMC_EVENT_SYSTEM_STATE_NOTIFY;
        apple_rtkit_send_user_msg(rtk, kSMCKeyEndpoint, r.raw);
        return SMC_RESULT_SUCCESS;
    }
    default:
        return SMC_RESULT_BAD_FUNC_PARAMETER;
    }
}

static void apple_smc_handle_key_endpoint(void *opaque, const uint32_t ep,
                                          const uint64_t msg)
{
    AppleRTKit *rtk = opaque;
    AppleSMCState *s = opaque;
    KeyMessage *kmsg;
    uint32_t key;
    KeyResponse resp = { 0 };
    SMCKey *key_entry;
    SMCKeyData *data_entry;

    kmsg = (KeyMessage *)&msg;

    key = le32_to_cpu(kmsg->key);

    SMC_LOG_MSG(ep, msg);

    resp.tag_and_id = kmsg->tag_and_id;

    switch (kmsg->cmd) {
    case SMC_GET_SRAM_ADDR:
        apple_rtkit_send_user_msg(rtk, ep, s->iomems[APPLE_SMC_MMIO_SRAM].addr);
        break;
    case SMC_READ_KEY:
    case SMC_READ_KEY_PAYLOAD: {
        key_entry = apple_smc_get_key(s, key);
        data_entry = apple_smc_get_key_data(s, key);
        if (key_entry == NULL) {
            resp.status = SMC_RESULT_KEY_NOT_FOUND;
        } else if (key_entry->info.attr & SMC_ATTR_R) {
            if (key_entry->read != NULL) {
                resp.status =
                    key_entry->read(key_entry, data_entry,
                                    kmsg->payload_length == 0 ? NULL : s->sram,
                                    kmsg->payload_length);
            }
            if (resp.status == SMC_RESULT_SUCCESS) {
                resp.status = apple_smc_key_read(
                    key_entry, data_entry, kmsg->length,
                    key_entry->info.size <= 4 ? resp.response : s->sram);
            }
            if (resp.status == SMC_RESULT_SUCCESS) {
                resp.length = key_entry->info.size;
            }
        } else {
            resp.status = SMC_RESULT_KEY_NOT_READABLE;
        }
        apple_rtkit_send_user_msg(rtk, ep, resp.raw);
        break;
    }
    case SMC_WRITE_KEY: {
        key_entry = apple_smc_get_key(s, key);
        data_entry = apple_smc_get_key_data(s, key);
        if (key_entry == NULL) {
            resp.status = SMC_RESULT_KEY_NOT_FOUND;
        } else if (key_entry->info.attr & SMC_ATTR_W) {
            if (key_entry->info.size != kmsg->length) {
                resp.status = SMC_RESULT_KEY_SIZE_MISMATCH;
            } else if (key_entry->write != NULL) {
                resp.status = key_entry->write(
                    key_entry, data_entry, kmsg->length == 0 ? NULL : s->sram,
                    kmsg->length);
            } else {
                memcpy(data_entry->data, s->sram, kmsg->length);
            }
            if (resp.status == SMC_RESULT_SUCCESS) {
                resp.length = key_entry->info.size;
            }
        } else {
            resp.status = SMC_RESULT_KEY_NOT_WRITABLE;
        }
        apple_rtkit_send_user_msg(rtk, ep, resp.raw);
        break;
    }
    case SMC_GET_KEY_BY_INDEX: {
        key_entry = QTAILQ_FIRST(&s->keys);

        for (int i = 0; i < key && key_entry != NULL; i++) {
            key_entry = QTAILQ_NEXT(key_entry, next);
        }

        if (key_entry == NULL) {
            resp.status = SMC_RESULT_KEY_INDEX_RANGE_ERROR;
        } else {
            resp.status = SMC_RESULT_SUCCESS;
            stl_le_p(resp.response, cpu_to_le32(key_entry->key));
        }

        apple_rtkit_send_user_msg(rtk, ep, resp.raw);
        break;
    }
    case SMC_GET_KEY_INFO: {
        key_entry = apple_smc_get_key(s, key);
        if (key_entry == NULL) {
            resp.status = SMC_RESULT_KEY_NOT_FOUND;
        } else {
            *(SMCKeyInfo *)s->sram = key_entry->info;
            resp.status = SMC_RESULT_SUCCESS;
        }
        apple_rtkit_send_user_msg(rtk, ep, resp.raw);
        break;
    }
    default: {
        resp.status = SMC_RESULT_BAD_COMMAND;
        apple_rtkit_send_user_msg(rtk, ep, resp.raw);
        fprintf(stderr, "SMC: Unknown command 0x%02x\n", kmsg->cmd);
        break;
    }
    }
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

static void apple_smc_boot_done(void *opaque)
{
    AppleSMCState *s = opaque;
    s->is_booted = true;
}

static const AppleRTKitOps apple_smc_rtkit_ops = {
    .boot_done = apple_smc_boot_done,
};

SysBusDevice *apple_smc_create(AppleDTNode *node, AppleA7IOPVersion version,
                               uint64_t sram_size)
{
    DeviceState *dev;
    AppleSMCState *s;
    AppleRTKit *rtk;
    SysBusDevice *sbd;
    AppleDTNode *child;
    AppleDTProp *prop;
    uint64_t *reg;
    uint8_t data[8] = { 0x40, 0x19, 0x01, 0x00, 0x80, 0x70, 0x00, 0x00 };
    uint8_t ac_adapter_count = 1;
    int8_t ac_w = 0x1; // should actually be a function
    uint8_t batt_feature_flags = 0x0;
    uint16_t batt_cycle_count = 0x7;
    uint16_t batt_avg_time_to_full = 0xffff; // not charging
    uint16_t batt_max_capacity = 31337;
    uint16_t batt_full_charge_capacity = batt_max_capacity * 0.98;
    // *0.69 shows as 67%/68% (console debug output) with full_charge_capacity
    // of 98%
    uint16_t batt_current_capacity = batt_full_charge_capacity * 0.69;
    uint16_t batt_remaining_capacity =
        batt_full_charge_capacity - batt_current_capacity;
    // b0fv might mean "battery full voltage"
    uint32_t b0fv = 0x201;
    uint8_t battery_count = 0x1;
    uint16_t batt_cell_voltage = 4200;
    int16_t batt_actual_amperage = 0x0;
    uint16_t batt_actual_voltage = batt_cell_voltage;

    g_assert_cmphex(sram_size, <=, UINT32_MAX);

    dev = qdev_new(TYPE_APPLE_SMC_IOP);
    s = APPLE_SMC_IOP(dev);
    rtk = APPLE_RTKIT(dev);
    sbd = SYS_BUS_DEVICE(dev);

    child = apple_dt_get_node(node, "iop-smc-nub");
    g_assert_nonnull(child);

    prop = apple_dt_get_prop(node, "reg");
    g_assert_nonnull(prop);

    reg = (uint64_t *)prop->data;

    apple_rtkit_init(rtk, NULL, "SMC", reg[1], version, &apple_smc_rtkit_ops);
    apple_rtkit_register_user_ep(rtk, kSMCKeyEndpoint, s,
                                 &apple_smc_handle_key_endpoint);

    memory_region_init_io(&s->iomems[APPLE_SMC_MMIO_ASC], OBJECT(dev),
                          &ascv2_core_reg_ops, s,
                          TYPE_APPLE_SMC_IOP ".ascv2-core-reg", reg[3]);
    sysbus_init_mmio(sbd, &s->iomems[APPLE_SMC_MMIO_ASC]);

    s->sram = qemu_memalign(qemu_real_host_page_size(), sram_size);
    s->sram_size = sram_size;
    memory_region_init_ram_device_ptr(&s->iomems[APPLE_SMC_MMIO_SRAM],
                                      OBJECT(dev), TYPE_APPLE_SMC_IOP ".sram",
                                      s->sram_size, s->sram);
    sysbus_init_mmio(sbd, &s->iomems[APPLE_SMC_MMIO_SRAM]);

    apple_dt_set_prop_u32(child, "pre-loaded", 1);
    apple_dt_set_prop_u32(child, "running", 1);

    QTAILQ_INIT(&s->keys);
    QTAILQ_INIT(&s->key_data);

    apple_smc_add_key_func(s, '#KEY', 4, SMC_KEY_TYPE_UINT32, 0, s,
                           smc_key_count_read, NULL);

    apple_smc_add_key(s, 'CLKH', 8, SMC_KEY_TYPE_CLH, SMC_ATTR_RW_LE, data);

    data[0] = 3;
    apple_smc_add_key(s, 'RGEN', 1, SMC_KEY_TYPE_UINT8, SMC_ATTR_R, data);

    apple_smc_add_key(s, 'aDC#', 4, SMC_KEY_TYPE_UINT32, SMC_ATTR_R, NULL);

    // seems to be readable, too
    apple_smc_add_key_func(s, 'MBSE', 4, SMC_KEY_TYPE_HEX, SMC_ATTR_LE, s, NULL,
                           apple_smc_mbse_write);

    apple_smc_add_key(s, 'LGPB', 1, SMC_KEY_TYPE_FLAG, SMC_ATTR_RW, NULL);
    apple_smc_add_key(s, 'LGPE', 1, SMC_KEY_TYPE_FLAG, SMC_ATTR_RW, NULL);

    // should actually be a function for event notifications
    apple_smc_add_key(s, 'NESN', 4, SMC_KEY_TYPE_HEX, SMC_ATTR_W_LE, NULL);

    apple_smc_add_key(s, 'AC-N', sizeof(ac_adapter_count), SMC_KEY_TYPE_UINT8,
                      SMC_ATTR_R, &ac_adapter_count);

    // all below should actually be a function
    apple_smc_add_key(s, 'AC-W', sizeof(ac_w), SMC_KEY_TYPE_SINT8, SMC_ATTR_R,
                      &ac_w);
    apple_smc_add_key(s, 'CHAI', 4, SMC_KEY_TYPE_UINT32, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TG0B', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TG0V', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    // ----

    apple_smc_add_key(s, 'TP1A', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TP2C', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TP1d', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TP2d', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TP3d', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TP4d', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TP5d', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TP3R', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TP4H', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'TP0Z', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_R_LE, NULL);

    apple_smc_add_key(s, 'B0AP', 4, SMC_KEY_TYPE_SINT32, SMC_ATTR_R_LE, NULL);

    apple_smc_add_key(s, 'Th0a', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Th1a', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Th2a', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Th0f', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Th1f', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Th2f', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Th0x', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Th1x', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Th2x', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Tc0a', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Tc1a', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Tc2a', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Tc0f', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Tc1f', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Tc2f', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Tc0x', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Tc1x', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'Tc2x', 4, SMC_KEY_TYPE_FLT, SMC_ATTR_R_LE, NULL);

    apple_smc_add_key(s, 'D0VR', 2, SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'D1VR', 2, SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'D2VR', 2, SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE, NULL);

    apple_smc_add_key(s, 'TV0s', 8, SMC_KEY_TYPE_IOFLT, SMC_ATTR_RW_LE, NULL);

    apple_smc_add_key(s, 'BHTL', 1, SMC_KEY_TYPE_FLAG, SMC_ATTR_RW_LE, NULL);

    // should actually be a function
    apple_smc_add_key(s, 'BFS0', sizeof(batt_feature_flags), SMC_KEY_TYPE_UINT8,
                      SMC_ATTR_R_LE, &batt_feature_flags);

    apple_smc_add_key(s, 'B0CT', sizeof(batt_cycle_count), SMC_KEY_TYPE_UINT16,
                      SMC_ATTR_R_LE, &batt_cycle_count);
    apple_smc_add_key(s, 'B0TF', sizeof(batt_avg_time_to_full),
                      SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE,
                      &batt_avg_time_to_full);
    apple_smc_add_key(s, 'B0CM', sizeof(batt_max_capacity), SMC_KEY_TYPE_UINT16,
                      SMC_ATTR_R_LE, &batt_max_capacity);
    apple_smc_add_key(s, 'B0FC', sizeof(batt_full_charge_capacity),
                      SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE,
                      &batt_full_charge_capacity);
    apple_smc_add_key(s, 'B0UC', sizeof(batt_current_capacity),
                      SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE,
                      &batt_current_capacity);
    apple_smc_add_key(s, 'B0RM', sizeof(batt_remaining_capacity),
                      SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE,
                      &batt_remaining_capacity);
    // should actually be a function
    apple_smc_add_key(s, 'B0FV', sizeof(b0fv), SMC_KEY_TYPE_HEX, SMC_ATTR_R_LE,
                      &b0fv);
    uint8_t bdd1 = 0x19;
    apple_smc_add_key(s, 'BDD1', sizeof(bdd1), SMC_KEY_TYPE_UINT8,
                      SMC_ATTR_R_LE, &bdd1);
    // should actually be a function
    apple_smc_add_key(s, 'UB0C', 1, SMC_KEY_TYPE_UINT8, SMC_ATTR_W_LE, NULL);
    apple_smc_add_key(s, 'BNCB', sizeof(battery_count), SMC_KEY_TYPE_UINT8,
                      SMC_ATTR_R_LE, &battery_count);
    apple_smc_add_key(s, 'BC1V', sizeof(batt_cell_voltage), SMC_KEY_TYPE_UINT16,
                      SMC_ATTR_R_LE, &batt_cell_voltage);
    apple_smc_add_key(s, 'BC2V', sizeof(batt_cell_voltage), SMC_KEY_TYPE_UINT16,
                      SMC_ATTR_R_LE, &batt_cell_voltage);
    apple_smc_add_key(s, 'BC3V', sizeof(batt_cell_voltage), SMC_KEY_TYPE_UINT16,
                      SMC_ATTR_R_LE, &batt_cell_voltage);
    apple_smc_add_key(s, 'BC4V', sizeof(batt_cell_voltage), SMC_KEY_TYPE_UINT16,
                      SMC_ATTR_R_LE, &batt_cell_voltage);
    uint16_t b0dc = 0xEF13;
    apple_smc_add_key(s, 'B0DC', sizeof(b0dc), SMC_KEY_TYPE_UINT16,
                      SMC_ATTR_R_LE, &b0dc);
    apple_smc_add_key(s, 'B0BL', 2, SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'B0CA', 2, SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'B0NC', 2, SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'B0IV', 2, SMC_KEY_TYPE_SINT16, SMC_ATTR_R_LE, NULL);
    apple_smc_add_key(s, 'B0AC', sizeof(batt_actual_amperage),
                      SMC_KEY_TYPE_SINT16, SMC_ATTR_R_LE,
                      &batt_actual_amperage);
    apple_smc_add_key(s, 'B0AV', sizeof(batt_actual_voltage),
                      SMC_KEY_TYPE_UINT16, SMC_ATTR_R_LE, &batt_actual_voltage);
    uint64_t chnc = 0x1; // ???
    apple_smc_add_key(s, 'CHNC', sizeof(chnc), SMC_KEY_TYPE_HEX, SMC_ATTR_R_LE,
                      &chnc);
    // should actually be a function
    apple_smc_add_key(s, 'CHAS', 4, SMC_KEY_TYPE_UINT32, SMC_ATTR_R_LE, NULL);
    // settings (as a whole) won't open/will crash if cha1 is missing
    // maybe the settings and safari crashes are unrelated from smc
    // should actually be a function
    apple_smc_add_key(s, 'CHA1', 4, SMC_KEY_TYPE_UINT32, SMC_ATTR_R_LE, NULL);
    // TODO: BHT0 battery heat map function, length 0x19/25
    // TODO: battery settings page won't fully load

    return sbd;
}

static const VMStateDescription vmstate_apple_smc_key_data = {
    .name = "SMCKeyData",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32(key, SMCKeyData),
            VMSTATE_UINT32(size, SMCKeyData),
            VMSTATE_VBUFFER_ALLOC_UINT32(data, SMCKeyData, 0, NULL, size),
            VMSTATE_END_OF_LIST(),
        },
};

static int vmstate_apple_smc_post_load(void *opaque, int version_id)
{
    AppleSMCState *s = opaque;
    SMCKey *key;
    SMCKey *key_next;
    SMCKeyData *data;
    SMCKeyData *data_next;

    QTAILQ_FOREACH_SAFE (data, &s->key_data, next, data_next) {
        key = apple_smc_get_key(s, data->key);
        if (key == NULL) {
            fprintf(stderr,
                    "Key `%c%c%c%c` was removed, state cannot be loaded.\n",
                    SMC_KEY_FORMAT(data->key));
            return -1;
        }

        if (key->info.size != data->size) {
            fprintf(stderr,
                    "Key `%c%c%c%c` has mismatched length, state cannot be "
                    "loaded.\n",
                    SMC_KEY_FORMAT(key->key));
            return -1;
        }
    }

    QTAILQ_FOREACH_SAFE (key, &s->keys, next, key_next) {
        if (apple_smc_get_key_data(s, key->key) == NULL) {
            fprintf(stderr,
                    "New key `%c%c%c%c` encountered, state cannot be loaded.\n",
                    SMC_KEY_FORMAT(key->key));
            return -1;
        }
    }

    return 0;
}

static const VMStateDescription vmstate_apple_smc = {
    .name = "AppleSMCState",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = vmstate_apple_smc_post_load,
    .fields =
        (const VMStateField[]){
            VMSTATE_APPLE_RTKIT(parent_obj, AppleSMCState),
            VMSTATE_QTAILQ_V(key_data, AppleSMCState, 0,
                             vmstate_apple_smc_key_data, SMCKeyData, next),
            VMSTATE_UINT32(sram_size, AppleSMCState),
            VMSTATE_VBUFFER_ALLOC_UINT32(sram, AppleSMCState, 0, NULL,
                                         sram_size),
            VMSTATE_END_OF_LIST(),
        },
};

static void apple_smc_reset_hold(Object *obj, ResetType type)
{
    AppleRTKitClass *rtkc;
    AppleSMCState *s;

    rtkc = APPLE_RTKIT_GET_CLASS(obj);
    s = APPLE_SMC_IOP(obj);

    if (rtkc->parent_phases.hold != NULL) {
        rtkc->parent_phases.hold(obj, type);
    }

    memset(s->sram, 0, s->sram_size);
    s->is_booted = false;
}

static void apple_smc_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc;
    DeviceClass *dc;
    AppleSMCClass *smcc;

    rc = RESETTABLE_CLASS(klass);
    dc = DEVICE_CLASS(klass);
    smcc = APPLE_SMC_IOP_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, apple_smc_reset_hold, NULL,
                                       &smcc->parent_phases);

    dc->desc = "Apple System Management Controller IOP";
    dc->vmsd = &vmstate_apple_smc;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo apple_smc_info = {
    .name = TYPE_APPLE_SMC_IOP,
    .parent = TYPE_APPLE_RTKIT,
    .instance_size = sizeof(AppleSMCState),
    .class_size = sizeof(AppleSMCClass),
    .class_init = apple_smc_class_init,
};

static void apple_smc_register_types(void)
{
    type_register_static(&apple_smc_info);
}

type_init(apple_smc_register_types);
