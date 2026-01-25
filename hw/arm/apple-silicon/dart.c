/*
 * Apple Device Address Resolution Table.
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
#include "block/aio.h"
#include "hw/arm/apple-silicon/dart.h"
#include "hw/arm/apple-silicon/dt.h"
#include "hw/irq.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "monitor/hmp-target.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "system/dma.h"
#include "qobject/qdict.h"

#if 0
#define DPRINTF(fmt, ...)                             \
    do {                                              \
        fprintf(stderr, "dart: " fmt, ##__VA_ARGS__); \
    } while (0)
#else
#define DPRINTF(fmt, ...) \
    do {                  \
    } while (0)
#endif

#define DART_MAX_STREAMS (16)
#define DART_MAX_TTBR (4)
#define DART_MAX_VA_BITS (38)
#define REG_DART_PARAMS1 (0x0)
#define DART_PARAMS1_PAGE_SHIFT(_x) (((_x) & 0xF) << 24)
#define REG_DART_PARAMS2 (0x4)
#define DART_PARAMS2_BYPASS_SUPPORT BIT32(0)
#define DART_PARAMS2_LOCK_SUPPORT BIT32(1)
#define REG_DART_TLB_OP (0x20)
#define DART_TLB_OP_BUSY BIT32(2)
#define DART_TLB_OP_INVALIDATE BIT32(20)
#define REG_DART_SID_MASK_LOW (0x34)
#define REG_DART_SID_MASK_HIGH (0x38)
#define REG_DART_ERROR_STATUS (0x40)
#define DART_ERROR_STREAM_SHIFT (24)
#define DART_ERROR_STREAM_LENGTH (4)
#define DART_ERROR_FLAG BIT32(31)
#define DART_ERROR_APF_REJECT BIT32(11)
#define DART_ERROR_UNKNOWN BIT32(9)
#define DART_ERROR_CTRR_WRITE_PROT BIT32(8)
#define DART_ERROR_REGION_PROT BIT32(7)
#define DART_ERROR_AXI_SLV_ERR BIT32(6)
#define DART_ERROR_AXI_SLV_DECODE BIT32(5)
#define DART_ERROR_READ_PROT BIT32(4)
#define DART_ERROR_WRITE_PROT BIT32(3)
#define DART_ERROR_PTE_INVLD BIT32(2)
#define DART_ERROR_L2E_INVLD BIT32(1)
#define DART_ERROR_TTBR_INVLD BIT32(0)
#define REG_DART_ERROR_ADDRESS_LOW (0x50)
#define REG_DART_ERROR_ADDRESS_HIGH (0x54)
#define REG_DART_CONFIG (0x60)
#define DART_CONFIG_LOCK BIT32(15)
#define REG_DART_SID_REMAP(sid) (0x80 + (4 * (sid)))
#define REG_DART_SID_VALID (0xFC)
#define REG_DART_SID_CONFIG(sid) (0x100 + (4 * (sid)))
#define DART_SID_CONFIG_DISABLE_TTBR_INVALID_ERR BIT32(0)
#define DART_SID_CONFIG_DISABLE_STE_INVALID_ERR BIT32(1)
#define DART_SID_CONFIG_DISABLE_PTE_INVALID_ERR BIT32(2)
#define DART_SID_CONFIG_DISABLE_WRITE_PROTECT_EXCEPTION BIT32(3)
#define DART_SID_CONFIG_DISABLE_READ_PROTECT_EXCEPTION BIT32(4)
#define DART_SID_CONFIG_DISABLE_AXI_RRESP_EXCEPTION BIT32(6)
#define DART_SID_CONFIG_TRANSLATION_ENABLE BIT32(7)
#define DART_SID_CONFIG_FULL_BYPASS BIT32(8)
#define DART_SID_CONFIG_DISABLE_DROP_PROTECT_EXCEPTION BIT32(9)
#define DART_SID_CONFIG_DISABLE_APF_REJECT_EXCEPTION BIT32(10)
#define DART_SID_CONFIG_APF_BYPASS BIT32(12)
#define DART_SID_CONFIG_BYPASS_ADDR_39_32_SHIFT (16)
#define DART_SID_CONFIG_BYPASS_ADDR_32_32_MASK (0xF)
#define REG_DART_TLB_CONFIG(sid) (0x180 + (4 * (sid)))
#define REG_DART_TTBR(sid, idx)            \
    (0x200 + ((DART_MAX_STREAMS * (sid)) + \
              (DART_MAX_TTBR * (idx)) * sizeof(uint32_t)))
#define DART_TTBR_VALID BIT32(31)
#define DART_TTBR_SHIFT (12)
#define DART_TTBR_MASK (0xFFFFFFF)
#define DART_PTE_NO_WRITE BIT32(7)
#define DART_PTE_NO_READ BIT32(8)
#define DART_PTE_AP_MASK (3 << 7)
#define DART_PTE_VALID (1 << 0)
#define DART_PTE_TYPE_TABLE (1 << 0)
#define DART_PTE_TYPE_BLOCK (3 << 0)
#define DART_PTE_TYPE_MASK (0x3)
#define DART_PTE_ADDR_MASK (0xFFFFFFFFFFull)
#define REG_DART_PERF_STATUS (0x100C)

#define DART_IOTLB_SID_SHIFT (53)
#define DART_IOTLB_SID_MASK (0xFull)
#define DART_IOTLB_SID(_x) \
    (((_x) & DART_IOTLB_SID_MASK) << DART_IOTLB_SID_SHIFT)
#define GET_DART_IOTLB_SID(_x) \
    (((_x) >> DART_IOTLB_SID_SHIFT) & DART_IOTLB_SID_MASK)

typedef enum {
    DART_UNKNOWN = 0,
    DART_DART,
    DART_SMMU,
    DART_DAPF,
} dart_instance_t;

static const char *dart_instance_name[] = {
    [DART_UNKNOWN] = "Unknown",
    [DART_DART] = "DART",
    [DART_SMMU] = "SMMU",
    [DART_DAPF] = "DAPF",
};

typedef struct AppleDARTTLBEntry {
    hwaddr block_addr;
    IOMMUAccessFlags perm;
} AppleDARTTLBEntry;

typedef struct AppleDARTMapperInstance AppleDARTMapperInstance;

typedef struct AppleDARTIOMMUMemoryRegion {
    IOMMUMemoryRegion iommu;
    AppleDARTMapperInstance *mapper;
    uint32_t sid;
} AppleDARTIOMMUMemoryRegion;

typedef struct {
    uint32_t params1;
    uint32_t params2;
    uint32_t tlb_op;
    uint64_t sid_mask;
    uint32_t error_status;
    uint64_t error_address;
    uint32_t config;
    uint8_t sid_remap[DART_MAX_STREAMS];
    uint32_t sid_config[DART_MAX_STREAMS];
    uint32_t ttbr[DART_MAX_STREAMS][DART_MAX_TTBR];
} AppleDARTDARTRegs;

typedef struct {
    MemoryRegion iomem;
    QemuMutex mutex;
    AppleDARTState *dart;
    uint32_t id;
    dart_instance_t type;
} AppleDARTInstance;

struct AppleDARTMapperInstance {
    AppleDARTInstance common;
    AppleDARTIOMMUMemoryRegion *iommus[DART_MAX_STREAMS];
    GHashTable *tlb;
    QEMUBH *invalidate_bh;
    AppleDARTDARTRegs regs;
};

struct AppleDARTState {
    SysBusDevice parent_obj;
    qemu_irq irq;
    AppleDARTInstance **instances;
    uint32_t num_instances;
    uint32_t page_size;
    uint32_t page_shift;
    uint64_t page_mask;
    uint64_t page_bits;
    uint32_t l_mask[3];
    uint32_t l_shift[3];
    uint64_t sid_mask;
    uint32_t bypass_mask;
    // uint64_t bypass_address;
    uint32_t dart_options;
};

static int apple_dart_device_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_APPLE_DART)) {
        *list = g_slist_append(*list, DEVICE(obj));
    }

    object_child_foreach(obj, apple_dart_device_list, opaque);
    return 0;
}

static GSList *apple_dart_get_device_list(void)
{
    GSList *list = NULL;

    object_child_foreach(qdev_get_machine(), apple_dart_device_list, &list);

    return list;
}

static void apple_dart_update_irq(AppleDARTState *dart)
{
    AppleDARTInstance *instance;
    AppleDARTMapperInstance *mapper;

    g_assert_nonnull(dart);

    for (uint32_t i = 0; i < dart->num_instances; i++) {
        instance = dart->instances[i];
        g_assert_nonnull(instance);
        if (instance->type != DART_DART) {
            continue;
        }

        mapper = container_of(instance, AppleDARTMapperInstance, common);

        if (mapper->regs.error_status == 0) {
            continue;
        }

        qemu_irq_raise(dart->irq);
        return;
    }

    qemu_irq_lower(dart->irq);
}

static gboolean apple_dart_tlb_remove_by_sid_mask(gpointer key, gpointer value,
                                                  gpointer user_data)
{
    hwaddr va = (hwaddr)key;

    return (1ULL << GET_DART_IOTLB_SID(va)) & GPOINTER_TO_UINT(user_data);
}

static void apple_dart_invalidate_bh(void *opaque)
{
    AppleDARTMapperInstance *mapper = opaque;
    uint64_t sid_mask;
    IOMMUTLBEvent event = { 0 };
    int i;

    WITH_QEMU_LOCK_GUARD(&mapper->common.mutex)
    {
        sid_mask = mapper->regs.sid_mask;
        g_hash_table_foreach_remove(mapper->tlb,
                                    apple_dart_tlb_remove_by_sid_mask,
                                    GUINT_TO_POINTER(sid_mask));
    }

    for (i = 0; i < DART_MAX_STREAMS; i++) {
        if ((sid_mask & (1ULL << i)) && mapper->iommus[i]) {
            event.type = IOMMU_NOTIFIER_UNMAP;
            event.entry.target_as = &address_space_memory;
            event.entry.iova = 0;
            event.entry.perm = IOMMU_NONE;
            event.entry.addr_mask = ~(hwaddr)0;

            memory_region_notify_iommu(&mapper->iommus[i]->iommu, 0, event);
        }
    }

    WITH_QEMU_LOCK_GUARD(&mapper->common.mutex)
    {
        mapper->regs.tlb_op &= ~(DART_TLB_OP_INVALIDATE | DART_TLB_OP_BUSY);
    }
}

static void apple_dart_mapper_reg_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    AppleDARTMapperInstance *mapper = opaque;
    uint32_t val = data;
    hwaddr i;

    DPRINTF("%s[%d]: (DART) 0x" HWADDR_FMT_plx " <- 0x" HWADDR_FMT_plx "\n",
            s->name, o->id, addr, data);

    QEMU_LOCK_GUARD(&mapper->common.mutex);

    switch (addr) {
    case REG_DART_PARAMS1:
        mapper->regs.params1 = val;
        break;
    case REG_DART_PARAMS2:
        mapper->regs.params2 = val;
        break;
    case REG_DART_TLB_OP:
        if ((val & DART_TLB_OP_INVALIDATE) == 0 ||
            (mapper->regs.tlb_op & DART_TLB_OP_BUSY) != 0) {
            break;
        }

        mapper->regs.tlb_op |= DART_TLB_OP_BUSY;

        qemu_bh_schedule(mapper->invalidate_bh);
        break;
    case REG_DART_SID_MASK_LOW:
        mapper->regs.sid_mask = deposit64(mapper->regs.sid_mask, 0, 32, val);
        break;
    case REG_DART_SID_MASK_HIGH:
        mapper->regs.sid_mask = deposit64(mapper->regs.sid_mask, 32, 32, val);
        break;
    case REG_DART_ERROR_STATUS:
        mapper->regs.error_status &= ~val;
        apple_dart_update_irq(mapper->common.dart);
        break;
    case REG_DART_ERROR_ADDRESS_LOW:
        mapper->regs.error_address =
            deposit64(mapper->regs.error_address, 0, 32, val);
        break;
    case REG_DART_ERROR_ADDRESS_HIGH:
        mapper->regs.error_address =
            deposit64(mapper->regs.error_address, 32, 32, val);
        break;
    case REG_DART_CONFIG:
        mapper->regs.config = val;
        break;
    case REG_DART_SID_REMAP(0)...(REG_DART_SID_REMAP(DART_MAX_STREAMS) - 1):
        i = addr - REG_DART_SID_REMAP(0);
        *(uint32_t *)&mapper->regs.sid_remap[i] = val;
        break;
    case REG_DART_SID_CONFIG(0)...(REG_DART_SID_CONFIG(DART_MAX_STREAMS) - 1):
        i = (addr - REG_DART_SID_CONFIG(0)) / sizeof(uint32_t);
        mapper->regs.sid_config[i] = val;
        break;
    case REG_DART_TTBR(0, 0)...(REG_DART_TTBR(DART_MAX_STREAMS, DART_MAX_TTBR) -
                                1):
        i = (addr - REG_DART_TTBR(0, 0)) / sizeof(uint32_t);
        ((uint32_t *)mapper->regs.ttbr)[i] = val;
        break;
    default:
        break;
    }
}

static uint64_t apple_dart_mapper_reg_read(void *opaque, hwaddr addr,
                                           unsigned size)
{
    AppleDARTMapperInstance *mapper = opaque;
    uint32_t i;

    QEMU_LOCK_GUARD(&mapper->common.mutex);

    DPRINTF("%s[%d]: (DART) 0x" HWADDR_FMT_plx "\n", o->s->name, o->id, addr);

    switch (addr) {
    case REG_DART_PARAMS1:
        return mapper->regs.params1;
    case REG_DART_PARAMS2:
        return mapper->regs.params2;
    case REG_DART_TLB_OP:
        return mapper->regs.tlb_op;
    case REG_DART_SID_MASK_LOW:
        return extract64(mapper->regs.sid_mask, 0, 32);
    case REG_DART_SID_MASK_HIGH:
        return extract64(mapper->regs.sid_mask, 32, 32);
    case REG_DART_ERROR_STATUS:
        return mapper->regs.error_status;
    case REG_DART_ERROR_ADDRESS_LOW:
        return extract64(mapper->regs.error_address, 0, 32);
    case REG_DART_ERROR_ADDRESS_HIGH:
        return extract64(mapper->regs.error_address, 32, 32);
    case REG_DART_CONFIG:
        return mapper->regs.config;
    case REG_DART_SID_REMAP(0)...(REG_DART_SID_REMAP(DART_MAX_STREAMS) - 1):
        i = addr - REG_DART_SID_REMAP(0);
        return *(uint32_t *)&mapper->regs.sid_remap[i];
    case REG_DART_SID_CONFIG(0)...(REG_DART_SID_CONFIG(DART_MAX_STREAMS) - 1):
        i = (addr - REG_DART_SID_CONFIG(0)) / sizeof(uint32_t);
        return mapper->regs.sid_config[i];
    case REG_DART_TTBR(0, 0)...(REG_DART_TTBR(DART_MAX_STREAMS, DART_MAX_TTBR) -
                                1):
        i = (addr - REG_DART_TTBR(0, 0)) / sizeof(uint32_t);
        return ((uint32_t *)mapper->regs.ttbr)[i];
    default:
        return 0;
    }
}

static const MemoryRegionOps apple_dart_mapper_reg_ops = {
    .write = apple_dart_mapper_reg_write,
    .read = apple_dart_mapper_reg_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void apple_dart_dummy_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                       unsigned size)
{
    AppleDARTInstance *instance = opaque;

    QEMU_LOCK_GUARD(&instance->mutex);

    DPRINTF("%s[%d]: (%s) 0x" HWADDR_FMT_plx " <- 0x" HWADDR_FMT_plx "\n",
            o->dart->parent_obj.parent_obj.id, o->id,
            dart_instance_name[o->type], addr, data);
}

static uint64_t apple_dart_dummy_reg_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    AppleDARTInstance *instance = opaque;

    QEMU_LOCK_GUARD(&instance->mutex);

    DPRINTF("%s[%d]: (%s) 0x" HWADDR_FMT_plx "\n",
            o->dart->parent_obj.parent_obj.id, o->id,
            dart_instance_name[o->type], addr);

    return 0;
}

static const MemoryRegionOps apple_dart_dummy_reg_ops = {
    .write = apple_dart_dummy_reg_write,
    .read = apple_dart_dummy_reg_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static AppleDARTTLBEntry *apple_dart_mapper_ptw(AppleDARTMapperInstance *mapper,
                                                uint32_t sid, hwaddr iova,
                                                uint32_t *out_error_status)
{
    AppleDARTState *dart = mapper->common.dart;

    uint64_t idx = (iova & (dart->l_mask[0])) >> dart->l_shift[0];
    uint64_t pte;
    uint64_t pa;
    int level;
    AppleDARTTLBEntry *tlb_entry = NULL;
    uint32_t error_status;

    if (sid >= DART_MAX_STREAMS || (dart->sid_mask & BIT_ULL(sid)) == 0 ||
        idx >= DART_MAX_TTBR ||
        (mapper->regs.ttbr[sid][idx] & DART_TTBR_VALID) == 0) {
        error_status = DART_ERROR_FLAG | DART_ERROR_TTBR_INVLD;
        goto end;
    }

    pte = mapper->regs.ttbr[sid][idx];
    pa = (pte & DART_TTBR_MASK) << DART_TTBR_SHIFT;

    for (level = 1; level < 3; level++) {
        idx = (iova & (dart->l_mask[level])) >> dart->l_shift[level];
        pa += 8 * idx;

        if (dma_memory_read(&address_space_memory, pa, &pte, sizeof(pte),
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            error_status = DART_ERROR_FLAG | DART_ERROR_L2E_INVLD;
            goto end;
        }
        DPRINTF("%s: level: %d, pa: 0x" HWADDR_FMT_plx " pte: 0x%llx(0x%llx)\n",
                __func__, level, pa, pte, idx);

        if ((pte & DART_PTE_VALID) == 0) {
            error_status = DART_ERROR_FLAG | DART_ERROR_PTE_INVLD;
            goto end;
        }
        pa = pte & dart->page_mask & DART_PTE_ADDR_MASK;
    }

    tlb_entry = g_new(AppleDARTTLBEntry, 1);
    tlb_entry->block_addr = (pte & dart->page_mask & DART_PTE_ADDR_MASK);
    tlb_entry->perm = IOMMU_ACCESS_FLAG((pte & DART_PTE_NO_READ) == 0,
                                        (pte & DART_PTE_NO_WRITE) == 0);
    error_status = 0;

end:
    if (out_error_status) {
        *out_error_status = error_status;
    }

    return tlb_entry;
}

static IOMMUTLBEntry apple_dart_mapper_translate(IOMMUMemoryRegion *mr,
                                                 hwaddr addr,
                                                 IOMMUAccessFlags flag,
                                                 int iommu_idx)
{
    AppleDARTIOMMUMemoryRegion *iommu =
        container_of(mr, AppleDARTIOMMUMemoryRegion, iommu);
    AppleDARTMapperInstance *mapper = iommu->mapper;
    AppleDARTState *dart = mapper->common.dart;
    AppleDARTTLBEntry *tlb_entry = NULL;
    uint32_t sid = iommu->sid;
    uint64_t iova;
    uint64_t key;

    IOMMUTLBEntry entry = {
        .target_as = &address_space_memory,
        .iova = addr,
        .addr_mask = dart->page_bits,
        .perm = IOMMU_NONE,
    };

    g_assert_cmpuint(sid, <, DART_MAX_STREAMS);

    QEMU_LOCK_GUARD(&mapper->common.mutex);

    sid = mapper->regs.sid_remap[sid] & 0xF;

    // Disabled translation means bypass, not error
    if ((dart->bypass_mask & BIT32(sid)) != 0 ||
        (mapper->regs.sid_config[sid] & DART_SID_CONFIG_TRANSLATION_ENABLE) ==
            0 ||
        (mapper->regs.sid_config[sid] & DART_SID_CONFIG_FULL_BYPASS) != 0) {
        // if (s->bypass_address != 0) {
        //     entry.translated_addr = s->bypass_address + addr,
        //     entry.perm = IOMMU_RW;
        // }
        goto end;
    }

    iova = addr >> dart->page_shift;
    key = DART_IOTLB_SID(iommu->sid) | iova;

    tlb_entry = g_hash_table_lookup(mapper->tlb, GUINT_TO_POINTER(key));

    if (tlb_entry == NULL) {
        uint32_t status;
        if ((tlb_entry = apple_dart_mapper_ptw(mapper, sid, iova, &status))) {
            g_hash_table_insert(mapper->tlb, GUINT_TO_POINTER(key), tlb_entry);
            DPRINTF("%s[%d]: (%s) SID %u: 0x" HWADDR_FMT_plx
                    " -> 0x" HWADDR_FMT_plx " (%c%c)\n",
                    mapper->common.name, mapper->common.id,
                    dart_instance_name[mapper->common.type], iommu->sid, addr,
                    tlb_entry->block_addr | (addr & dart->page_bits),
                    (tlb_entry->perm & IOMMU_RO) ? 'r' : '-',
                    (tlb_entry->perm & IOMMU_WO) ? 'w' : '-');
        } else {
            mapper->regs.error_address = addr;
            mapper->regs.error_status = deposit32(
                mapper->regs.error_status | status, DART_ERROR_STREAM_SHIFT,
                DART_ERROR_STREAM_LENGTH, iommu->sid);
            goto end;
        }
    }

    entry.translated_addr = tlb_entry->block_addr | (addr & entry.addr_mask);
    entry.perm = tlb_entry->perm;

    if ((flag & IOMMU_WO) != 0 && (entry.perm & IOMMU_WO) == 0) {
        mapper->regs.error_address = addr;
        mapper->regs.error_status = deposit32(
            mapper->regs.error_status | DART_ERROR_FLAG | DART_ERROR_WRITE_PROT,
            DART_ERROR_STREAM_SHIFT, DART_ERROR_STREAM_LENGTH, iommu->sid);
    }

    if ((flag & IOMMU_RO) != 0 && (entry.perm & IOMMU_RO) == 0) {
        mapper->regs.error_address = addr;
        mapper->regs.error_status = deposit32(
            mapper->regs.error_status | DART_ERROR_FLAG | DART_ERROR_READ_PROT,
            DART_ERROR_STREAM_SHIFT, DART_ERROR_STREAM_LENGTH, iommu->sid);
    }

end:
    DPRINTF("%s[%d]: (%s) SID %u: 0x" HWADDR_FMT_plx " -> 0x" HWADDR_FMT_plx
            " (%c%c)\n",
            mapper->common.name, mapper->common.id,
            dart_instance_name[mapper->common.type], iommu->sid, entry.iova,
            entry.translated_addr, (entry.perm & IOMMU_RO) ? 'r' : '-',
            (entry.perm & IOMMU_WO) ? 'w' : '-');
    apple_dart_update_irq(dart);
    return entry;
}

static void apple_dart_reset(DeviceState *dev)
{
    AppleDARTState *dart = APPLE_DART(dev);
    AppleDARTInstance *instance;
    AppleDARTMapperInstance *mapper;
    uint32_t i;
    uint32_t j;

    for (i = 0; i < dart->num_instances; i++) {
        switch (dart->instances[i]->type) {
        case DART_DART: {
            instance = dart->instances[i];
            QEMU_LOCK_GUARD(&instance->mutex);
            mapper = container_of(instance, AppleDARTMapperInstance, common);
            memset(&mapper->regs, 0, sizeof(mapper->regs));

            // TODO: added hack against panic
            bool access_region_protection = (dart->dart_options & BIT(1)) != 0;
            mapper->regs.params1 = DART_PARAMS1_PAGE_SHIFT(dart->page_shift) |
                                   ((uint32_t)access_region_protection << 31);
            for (j = 0; j < DART_MAX_STREAMS; j++) {
                mapper->regs.sid_remap[j] = j;
            }

            if (mapper->tlb) {
                g_hash_table_destroy(mapper->tlb);
            }
            mapper->tlb = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                NULL, g_free);
            break;
        }
        default:
            break;
        }
    }
}

static void apple_dart_realize(DeviceState *dev, Error **errp)
{
}

IOMMUMemoryRegion *apple_dart_iommu_mr(AppleDARTState *dart, uint32_t sid)
{
    AppleDARTInstance *instance;
    AppleDARTMapperInstance *mapper;
    uint32_t i;

    if (dart->sid_mask & BIT_ULL(sid)) {
        for (i = 0; i < dart->num_instances; i++) {
            instance = dart->instances[i];
            if (instance->type != DART_DART) {
                continue;
            }

            mapper = container_of(instance, AppleDARTMapperInstance, common);
            return &mapper->iommus[sid]->iommu;
        }
    }
    return NULL;
}

IOMMUMemoryRegion *apple_dart_instance_iommu_mr(AppleDARTState *dart,
                                                uint32_t instance, uint32_t sid)
{
    AppleDARTInstance *o;
    AppleDARTMapperInstance *mapper;

    if (instance >= dart->num_instances ||
        (dart->sid_mask & BIT_ULL(sid)) == 0) {
        return NULL;
    }

    o = dart->instances[instance];
    if (o->type != DART_DART) {
        return NULL;
    }

    mapper = container_of(o, AppleDARTMapperInstance, common);
    return &mapper->iommus[sid]->iommu;
}

AppleDARTState *apple_dart_from_node(AppleDTNode *node)
{
    DeviceState *dev;
    AppleDARTState *dart;
    SysBusDevice *sbd;
    AppleDTProp *prop;
    uint64_t *reg;
    uint32_t *instance_data;
    int i;

    dev = qdev_new(TYPE_APPLE_DART);
    dart = APPLE_DART(dev);
    sbd = SYS_BUS_DEVICE(dev);

    dev->id = apple_dt_get_prop_strdup(node, "name", &error_fatal);

    dart->page_size =
        apple_dt_get_prop_u32_or(node, "page-size", 0x1000, &error_fatal);
    dart->page_shift = 31 - clz32(dart->page_size);
    dart->page_bits = dart->page_size - 1;
    dart->page_mask = ~dart->page_bits;

    switch (dart->page_shift) {
    case 12:
        memcpy(dart->l_mask, (uint32_t[3]){ 0xC0000, 0x3FE00, 0x1FF },
               sizeof(dart->l_mask));
        memcpy(dart->l_shift, (uint32_t[3]){ 0x12, 9, 0 },
               sizeof(dart->l_shift));
        break;
    case 14:
        memcpy(dart->l_mask, (uint32_t[3]){ 0xC00000, 0x3FF800, 0x7FF },
               sizeof(dart->l_mask));
        memcpy(dart->l_shift, (uint32_t[3]){ 0x16, 11, 0 },
               sizeof(dart->l_shift));
        break;
    default:
        g_assert_not_reached();
    }

    // NOTE: there can be up to 64 SIDs. Not on the currently-emulated hardware,
    // but other ones.
    dart->sid_mask =
        apple_dt_get_prop_u32_or(node, "sids", 0xFFFF, &error_fatal);
    dart->bypass_mask =
        apple_dt_get_prop_u32_or(node, "bypass", 0, &error_fatal);
    // s->bypass_address =
    //     apple_dt_get_prop_u64_or(node, "bypass-address", 0, &error_warn);
    dart->dart_options =
        apple_dt_get_prop_u32_or(node, "dart-options", 0, &error_fatal);

    prop = apple_dt_get_prop(node, "instance");
    if (prop == NULL) {
        if (apple_dt_get_prop_u32_or(node, "smmu-present", 0, &error_fatal) ==
            1) {
            instance_data = (uint32_t *)"TRADDART\0\0\0\0UMMSSMMU\0\0\0";
        } else {
            instance_data = (uint32_t *)"TRADDART\0\0\0";
        }
    } else {
        g_assert_cmpuint(prop->len % 12, ==, 0);
        instance_data = (uint32_t *)prop->data;
    }

    prop = apple_dt_get_prop(node, "reg");
    g_assert_nonnull(prop);

    reg = (uint64_t *)prop->data;

    dart->num_instances = prop->len / 16;
    dart->instances = g_new(AppleDARTInstance *, dart->num_instances);
    for (i = 0; i < dart->num_instances; i++) {
        AppleDARTInstance *instance;

        switch (ldl_le_p(instance_data)) {
        case 'DART': {
            AppleDARTMapperInstance *mapper =
                g_new0(AppleDARTMapperInstance, 1);
            instance = &mapper->common;
            instance->type = DART_DART;
            memory_region_init_io(&instance->iomem, OBJECT(dev),
                                  &apple_dart_mapper_reg_ops, instance,
                                  TYPE_APPLE_DART ".reg", reg[(i * 2) + 1]);
            mapper->invalidate_bh = aio_bh_new(
                qemu_get_aio_context(), apple_dart_invalidate_bh, instance);

            for (uint32_t sid = 0; sid < DART_MAX_STREAMS; sid++) {
                if (!(dart->sid_mask & BIT_ULL(sid))) {
                    continue;
                }

                char *name = g_strdup_printf("dart-%s-%u-%u", DEVICE(dart)->id,
                                             instance->id, sid);
                mapper->iommus[sid] = g_new0(AppleDARTIOMMUMemoryRegion, 1);
                mapper->iommus[sid]->sid = sid;
                mapper->iommus[sid]->mapper = mapper;
                memory_region_init_iommu(
                    mapper->iommus[sid], sizeof(AppleDARTIOMMUMemoryRegion),
                    TYPE_APPLE_DART_IOMMU_MEMORY_REGION, OBJECT(dart), name,
                    1ULL << DART_MAX_VA_BITS);
                g_free(name);
            }
            break;
        }
        case 'SMMU':
            instance = g_new0(AppleDARTInstance, 1);
            instance->type = DART_SMMU;
            goto common;
        case 'DAPF':
            instance = g_new0(AppleDARTInstance, 1);
            instance->type = DART_DAPF;
            goto common;
        default:
            instance = g_new0(AppleDARTInstance, 1);
            instance->type = DART_UNKNOWN;
        common:
            memory_region_init_io(&instance->iomem, OBJECT(dev),
                                  &apple_dart_dummy_reg_ops, instance,
                                  TYPE_APPLE_DART ".reg", reg[(i * 2) + 1]);
            break;
        }
        qemu_mutex_init(&instance->mutex);
        instance->id = i;
        instance->dart = dart;
        dart->instances[i] = instance;
        sysbus_init_mmio(sbd, &instance->iomem);
        DPRINTF("%s: DART %s instance %d: %s\n", __func__, instance->name, i,
                dart_instance_name[instance->type]);
        instance_data += 3;
    }

    sysbus_init_irq(sbd, &dart->irq);

    return dart;
}

static void apple_dart_dump_pt(Monitor *mon, AppleDARTInstance *instance,
                               hwaddr iova, const uint64_t *entries, int level,
                               uint64_t pte)
{
    AppleDARTState *dart = instance->dart;
    if (level == 3) {
        monitor_printf(mon,
                       "\t\t\t0x" HWADDR_FMT_plx " ... 0x" HWADDR_FMT_plx
                       " -> 0x%llx %c%c\n",
                       iova << dart->page_shift, (iova + 1) << dart->page_shift,
                       pte & dart->page_mask & DART_PTE_ADDR_MASK,
                       pte & DART_PTE_NO_READ ? '-' : 'r',
                       pte & DART_PTE_NO_WRITE ? '-' : 'w');
        return;
    }

    for (uint64_t i = 0; i <= (dart->l_mask[level] >> dart->l_shift[level]);
         i++) {
        uint64_t pte2 = entries[i];

        if ((pte2 & DART_PTE_VALID) ||
            ((level == 0) && (pte2 & DART_TTBR_VALID))) {
            uint64_t pa = pte2 & dart->page_mask & DART_PTE_ADDR_MASK;
            if (level == 0) {
                pa = (pte2 & DART_TTBR_MASK) << DART_TTBR_SHIFT;
            }
            uint64_t next_n_entries = 0;
            if (level < 2) {
                next_n_entries =
                    (dart->l_mask[level + 1] >> dart->l_shift[level + 1]) + 1;
            }
            g_autofree uint64_t *next = g_malloc0(8 * next_n_entries);
            if (dma_memory_read(&address_space_memory, pa, next,
                                8 * next_n_entries,
                                MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                continue;
            }

            apple_dart_dump_pt(mon, instance,
                               iova | (i << dart->l_shift[level]), next,
                               level + 1, pte2);
        }
    }
}

void hmp_info_dart(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_try_str(qdict, "name");
    g_autoptr(GSList) device_list = apple_dart_get_device_list();
    AppleDARTState *dart = NULL;

    if (name == NULL) {
        for (GSList *ele = device_list; ele; ele = ele->next) {
            DeviceState *dev = ele->data;
            dart = APPLE_DART(dev);
            monitor_printf(mon, "%s\tPage size: %d\t%d Instances\n", dev->id,
                           dart->page_size, dart->num_instances);
        }
        return;
    }

    for (GSList *ele = device_list; ele; ele = ele->next) {
        DeviceState *dev = ele->data;
        if (!strcmp(dev->id, name)) {
            dart = APPLE_DART(dev);
            break;
        }
    }

    if (dart == NULL) {
        monitor_printf(mon, "Cannot find dart %s\n", name);
        return;
    }

    for (uint32_t i = 0; i < dart->num_instances; i++) {
        AppleDARTInstance *instance = dart->instances[i];
        monitor_printf(mon, "\tInstance %d: type: %s\n", i,
                       dart_instance_name[instance->type]);
        if (instance->type != DART_DART) {
            continue;
        }
        AppleDARTMapperInstance *mapper =
            container_of(instance, AppleDARTMapperInstance, common);

        for (int sid = 0; sid < DART_MAX_STREAMS; sid++) {
            if (dart->sid_mask & BIT_ULL(sid)) {
                uint32_t remap = mapper->regs.sid_remap[sid] & 0xF;
                if (sid != remap) {
                    monitor_printf(mon, "\t\tSID %d: Remapped to %d\n", sid,
                                   remap);
                    continue;
                }
                if ((mapper->regs.sid_config[sid] &
                     DART_SID_CONFIG_TRANSLATION_ENABLE) == 0) {
                    monitor_printf(mon, "\t\tSID %d: Translation disabled\n",
                                   sid);
                    continue;
                }

                if (mapper->regs.sid_config[sid] &
                    DART_SID_CONFIG_FULL_BYPASS) {
                    monitor_printf(mon, "\t\tSID %d: Translation bypassed\n",
                                   sid);
                    continue;
                }
                monitor_printf(mon, "\t\tSID %d:\n", sid);
                const uint64_t l0_entries[] = { mapper->regs.ttbr[sid][0],
                                                mapper->regs.ttbr[sid][1],
                                                mapper->regs.ttbr[sid][2],
                                                mapper->regs.ttbr[sid][3] };
                apple_dart_dump_pt(mon, instance, 0, l0_entries, 0, 0);
            }
        }
    }
}

// static const VMStateDescription vmstate_apple_dart_instance = {
//     .name = "AppleDARTInstance",
//     .version_id = 1,
//     .minimum_version_id = 1,
//     .fields =
//         (const VMStateField[]){
//             VMSTATE_UINT32_ARRAY(base_reg, AppleDARTInstance,
//                                  0x4000 / sizeof(uint32_t)),
//             VMSTATE_END_OF_LIST(),
//         }
// };
//
// static const VMStateDescription vmstate_apple_dart = {
//     .name = "AppleDARTState",
//     .version_id = 1,
//     .minimum_version_id = 1,
//     .priority = MIG_PRI_IOMMU,
//     .fields =
//         (const VMStateField[]){
//             VMSTATE_STRUCT_ARRAY(instances, AppleDARTState,
//             DART_MAX_INSTANCE,
//                                  1, vmstate_apple_dart_instance,
//                                  AppleDARTInstance),
//             VMSTATE_END_OF_LIST(),
//         }
// };

static void apple_dart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = apple_dart_realize;
    device_class_set_legacy_reset(dc, apple_dart_reset);
    dc->desc = "Apple DART IOMMU";
    // dc->vmsd = &vmstate_apple_dart;
}

static void apple_dart_iommu_memory_region_class_init(ObjectClass *klass,
                                                      const void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = apple_dart_mapper_translate;
}

static const TypeInfo apple_dart_info = {
    .name = TYPE_APPLE_DART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AppleDARTState),
    .class_init = apple_dart_class_init,
};

static const TypeInfo apple_dart_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_APPLE_DART_IOMMU_MEMORY_REGION,
    .class_init = apple_dart_iommu_memory_region_class_init,
};

static void apple_dart_register_types(void)
{
    type_register_static(&apple_dart_info);
    type_register_static(&apple_dart_iommu_memory_region_info);
}

type_init(apple_dart_register_types);
