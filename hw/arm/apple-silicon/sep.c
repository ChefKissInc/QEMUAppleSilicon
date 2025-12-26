/*
 * Apple SEP.
 *
 * Copyright (c) 2023-2025 Visual Ehrmanntraut (VisualEhrmanntraut).
 * Copyright (c) 2023-2025 Christian Inci (chris-pcguy).
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
#include "block/aio.h"
#include "crypto/cipher.h"
#include "exec/cputlb.h"
#include "exec/tb-flush.h"
#include "hw/arm/apple-silicon/a13.h"
#include "hw/arm/apple-silicon/a9.h"
#include "hw/arm/apple-silicon/sep.h"
#include "hw/boards.h"
#include "hw/gpio/apple_gpio.h"
#include "hw/i2c/apple_i2c.h"
#include "hw/irq.h"
#include "hw/nvram/eeprom_at24c.h"
#include "hw/qdev-properties-system.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/guest-random.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/block-backend-global-state.h"
#include "system/block-backend-io.h"
#include "system/tcg.h"
#include "nettle/ccm.h"
#include "nettle/cmac.h"
#include "nettle/ecc-curve.h"
#include "nettle/ecdsa.h"
#include "nettle/hkdf.h"
#include "nettle/hmac.h"
#include "trace.h"
#include <nettle/macros.h>
#include <nettle/memxor.h>

#if 0
#define HEXDUMP(a, b, c) qemu_hexdump(stderr, a, b, c)
#define DPRINTF(v, ...) fprintf(stderr, v, ##__VA_ARGS__)
#else
#define HEXDUMP(a, b, c) \
    do {                 \
    } while (0)
#define DPRINTF(v, ...) \
    do {                \
    } while (0)
#endif

typedef struct {
    uint8_t ep;
    uint8_t tag;
    uint8_t op;
    uint8_t param;
    uint32_t data;
} QEMU_PACKED SEPMessage;

// #define ENABLE_CPU_DUMP_STATE

#define SEP_ENABLE_HARDCODED_FIRMWARE
// #define SEP_ENABLE_DEBUG_TRACE_MAPPING
// #define SEP_ENABLE_TRACE_BUFFER
// can cause conflicts with kernel and userspace, not anymore?
// #define SEP_ENABLE_OVERWRITE_SHMBUF_OBJECTS
// #define SEP_DISABLE_ASLR

// only used for SEP_ENABLE_TRACE_BUFFER and SEP_DISABLE_ASLR
#define SEP_USE_VERSION_OVERRIDE 14
// #define SEP_USE_VERSION_OVERRIDE 15
// #define SEP_USE_VERSION_OVERRIDE 16
// #define SEP_USE_VERSION_OVERRIDE 18

#define SEP_AESS_CMD_FLAG_KEYSIZE_AES128 0x0
#define SEP_AESS_CMD_FLAG_KEYSIZE_AES192 0x100
#define SEP_AESS_CMD_FLAG_KEYSIZE_AES256 0x200

#define SEP_AESS_CMD_FLAG_KEYSELECT_GID0_T8010 0x00 // ???
#define SEP_AESS_CMD_FLAG_KEYSELECT_GID1_T8010 0x10 // ???
#define SEP_AESS_CMD_FLAG_KEYSELECT_CUSTOM_T8010 0x20 // ???
#define SEP_AESS_CMD_FLAG_UNKNOWN0_T8010 0x00 // ???

#define SEP_AESS_CMD_FLAG_KEYSELECT_GID0_T8020 0x00 // also for T8015
#define SEP_AESS_CMD_FLAG_KEYSELECT_GID1_T8020 0x40 // also for T8015
// Also for T8015, this (custom) takes precedence over the other
// keyselect flags
#define SEP_AESS_CMD_FLAG_KEYSELECT_CUSTOM_T8020 0x80
#define SEP_AESS_CMD_FLAG_UNKNOWN0_T8020 0x10
#define SEP_AESS_CMD_FLAG_UNKNOWN1_T8020 0x20

#define SEP_AESS_CMD_FLAG_UNKNOWN0 SEP_AESS_CMD_FLAG_UNKNOWN0_T8020
#define SEP_AESS_CMD_FLAG_UNKNOWN1 SEP_AESS_CMD_FLAG_UNKNOWN1_T8020

#define SEP_AESS_CMD_FLAG_KEYSELECT_GID0 SEP_AESS_CMD_FLAG_KEYSELECT_GID0_T8020
#define SEP_AESS_CMD_FLAG_KEYSELECT_GID1 SEP_AESS_CMD_FLAG_KEYSELECT_GID1_T8020
#define SEP_AESS_CMD_FLAG_KEYSELECT_CUSTOM \
    SEP_AESS_CMD_FLAG_KEYSELECT_CUSTOM_T8020

#define SEP_AESS_CMD_MASK 0x3FF

#define SEP_AESS_CMD_WITHOUT_KEYSIZE(cmd)                                    \
    ((cmd) &                                                                 \
     ~(SEP_AESS_CMD_FLAG_KEYSIZE_AES256 | SEP_AESS_CMD_FLAG_KEYSIZE_AES192 | \
       SEP_AESS_CMD_FLAG_KEYSIZE_AES128))
#define SEP_AESS_CMD_WITHOUT_FLAGS(cmd)                                      \
    (SEP_AESS_CMD_WITHOUT_KEYSIZE(cmd) &                                     \
     ~(SEP_AESS_CMD_FLAG_KEYSELECT_GID0 | SEP_AESS_CMD_FLAG_KEYSELECT_GID1 | \
       SEP_AESS_CMD_FLAG_KEYSELECT_CUSTOM))
// #define SEP_AESS_CMD_WITHOUT_FLAGS(cmd) (cmd &
// ~(SEP_AESS_CMD_FLAG_KEYSIZE_AES256 | SEP_AESS_CMD_FLAG_KEYSIZE_AES192 |
// SEP_AESS_CMD_FLAG_UNKNOWN0))
////#define SEP_AESS_CMD_WITHOUT_FLAGS(cmd) (cmd &
///~(SEP_AESS_CMD_FLAG_KEYSIZE_AES256 | SEP_AESS_CMD_FLAG_KEYSIZE_AES192 |
/// SEP_AESS_CMD_FLAG_UNKNOWN0 | SEP_AESS_CMD_FLAG_UNKNOWN1))

#define SEP_AESS_CMD_FLAG_KEYSELECT_GID1_CUSTOM(cmd) \
    ((cmd) &                                         \
     (SEP_AESS_CMD_FLAG_KEYSELECT_GID1 | SEP_AESS_CMD_FLAG_KEYSELECT_CUSTOM))

// static keys: 0x00/0x40/0x80/0xc0
// seeded keys:
// wrapping key: key index 0: 0x01..0x05 ; key index 1: 0x41..0x45
// authentication key: key index 0: 0x81..0x04 ; key index 1: 0xc1..0x44

#define SEP_AESS_COMMAND_SYNC_SEEDBITS 0x0 // sync with register_seed_bits?
// usually in combination with SYNC_SEEDBITS
#define SEP_AESS_COMMAND_CREATE_KEY_FROM_SEED 0x3
// forces and overwrites flags, aes256 && custom. do nothing if the custom flag
// was set.
#define SEP_AESS_COMMAND_ENCRYPT_CBC_ONLY_NONCUSTOM_FORCE_CUSTOM_AES256 0x6
// forces and overwrites flags, aes256 && custom.
#define SEP_AESS_COMMAND_ENCRYPT_CBC_FORCE_CUSTOM_AES256 0x8
#define SEP_AESS_COMMAND_ENCRYPT_CBC 0x9
#define SEP_AESS_COMMAND_DECRYPT_CBC 0xA
#define SEP_AESS_COMMAND_0xB 0xB // custom aes key?


#define SEP_AESS_REGISTER_STATUS 0x4
#define SEP_AESS_REGISTER_COMMAND 0x8
#define SEP_AESS_REGISTER_INTERRUPT_STATUS 0xC
#define SEP_AESS_REGISTER_INTERRUPT_ENABLED 0x10
#define SEP_AESS_REGISTER_0x14_KEYWRAP_ITERATIONS_COUNTER 0x14
#define SEP_AESS_REGISTER_0x18_KEYDISABLE 0x18
#define SEP_AESS_REGISTER_SEED_BITS 0x1C
#define SEP_AESS_REGISTER_SEED_BITS_LOCK 0x20
#define SEP_AESS_REGISTER_IV 0x40
#define SEP_AESS_REGISTER_IN 0x50
#define SEP_AESS_REGISTER_TAG_OUT 0x60
#define SEP_AESS_REGISTER_OUT 0x70

#define SEP_AESS_REGISTER_STATUS_RUN_COMMAND BIT(0)
#define SEP_AESS_REGISTER_STATUS_ACTIVE BIT(1)
#define SEP_AESS_REGISTER_STATUS_UNKN0 BIT(8)
#define SEP_AESS_REGISTER_STATUS_UNKN1_ERROR BIT(9)
#define SEP_AESS_REGISTER_STATUS_UNKN2_ERROR BIT(10)

#define SEP_AESS_REGISTER_INTERRUPT_STATUS_DONE BIT(0)
#define SEP_AESS_REGISTER_INTERRUPT_STATUS_UNRECOVERABLE_ERROR_INTERRUPT BIT(1)

#define SEP_AESS_REGISTER_INTERRUPT_ENABLED_MASK 0x3
#define SEP_AESS_REGISTER_INTERRUPT_ENABLED_RAISE_ON_COMPLETION BIT(0)
#define SEP_AESS_REGISTER_INTERRUPT_ENABLED_INTERRUPT_ENABLED BIT(1)

#define SEP_AESS_SEED_BITS_BIT0 BIT(0)
#define SEP_AESS_SEED_BITS_BIT27 BIT(27) // cmds 0x50 and 0x90
#define SEP_AESS_SEED_BITS_BIT28 BIT(28) // invalid EKEY?
// DSEC tag present, demote SEP
#define SEP_AESS_SEED_BITS_SEP_DSEC_DEMOTED BIT(29)
// ap is prod-fused and demoted
#define SEP_AESS_SEED_BITS_AP_PROD_DEMOTED BIT(30)
#define SEP_AESS_SEED_BITS_IMG4_VERIFIED (1 << 31) // img4 verified?


// static uint32_t AESS_UID[0x20 / 4] = {0xDEADBEEF, 0x13371337, 0xA55A5AA5,
// 0xCAFECAFE, 0xC4F3C4F3, 0xD34DB33F, 0x73317331, 0x5AA5A55A};
static uint32_t AESS_UID0[0x20 / 4] = { 0xDEADBEEF, 0x13370000, 0xA55A0000,
                                        0xCAFECAFE, 0xC4F3C4F3, 0xD34DB33F,
                                        0xFF317331, 0xFFA50000 };
static uint32_t AESS_UID1[0x20 / 4] = { 0xDEADBEEF, 0x13371111, 0xA55A1111,
                                        0xCAFECAFE, 0xC4F3C4F3, 0xD34DB33F,
                                        0xFF317331, 0xFFA50000 };
static uint32_t AESS_GID0[0x20 / 4] = { 0xDEADBE00, 0x13371337, 0xA55A5AA5,
                                        0xCAFECA00, 0xC4F3C400, 0xD34DB33F,
                                        0x73317331, 0x5AA5A500 };
static uint32_t AESS_GID1[0x20 / 4] = { 0xDEADBE11, 0x13371337, 0xA55A5AA5,
                                        0xCAFECA11, 0xC4F3C411, 0xD34DB33F,
                                        0x73317331, 0x5AA5A511 };
static uint32_t AESS_KEY_FOR_DISABLED_KEY[0x20 / 4] = {
    0xF00FF00F, 0xF00FF00F, 0xF00FF00F, 0xCAFECA33,
    0xC4F3C488, 0xD34DB33F, 0xF00FF00F, 0xF00FF00F
};
static uint32_t AESS_UID_SEED_NOT_ENABLED[0x20 / 4] = {
    0x0FF00FF0, 0x0FF00FF0, 0x0FF00FF0, 0xCAFECA44,
    0xC4F3C499, 0xD34DB33F, 0x0FF00FF0, 0x0FF00FF0
};
static uint32_t AESS_UID_SEED_INVALID[0x20 / 4] = { 0x1FF11FF1, 0x1FF11FF1,
                                                    0x1FF11FF1, 0xCAFECA55,
                                                    0xC4F3C4AA, 0xD34DB33F,
                                                    0x1FF11FF1, 0x1FF11FF1 };

/* comment to prevent kate from adding whitespaces upon pressing enter */

#define SEP_PKA_STATUS_INTERRUPT_0xA 0x1
#define SEP_PKA_STATUS_INTERRUPT_0xB 0x2
#define SEP_PKA_STATUS_INTERRUPT_0xC 0x4

// for KEY_BASE register 0x0: key status
#define SEP_KEY_BASE_KEY_STATUS_OFFSET 0x0
#define SEP_KEY_BASE_KEY_STATUS_HDCP_FUSE_VALID BIT(0)
#define SEP_KEY_BASE_KEY_STATUS_LC128_VALID BIT(1)
#define SEP_KEY_BASE_KEY_STATUS_DPK_TX_VALID BIT(2)

#define SEP_KEY_BASE_KEY_STATUS_KM_VALID_INTERFACE_SHIFT 16
#define SEP_KEY_BASE_KEY_STATUS_KM_VALID_INTERFACE_MASK 0x7f
#define SEP_KEY_BASE_KEY_STATUS_KM_VALID_INTERFACE_GET(n)        \
    (((n) >> SEP_KEY_BASE_KEY_STATUS_KM_VALID_INTERFACE_SHIFT) & \
     SEP_KEY_BASE_KEY_STATUS_KM_VALID_INTERFACE_MASK)
#define SEP_KEY_BASE_KEY_STATUS_KM_VALID_INTERFACE_SET(n)    \
    (((n) & SEP_KEY_BASE_KEY_STATUS_KM_VALID_INTERFACE_MASK) \
     << SEP_KEY_BASE_KEY_STATUS_KM_VALID_INTERFACE_SHIFT)

#define SEP_KEY_BASE_KEY_STATUS_KS_VALID_INTERFACE_SHIFT 24
#define SEP_KEY_BASE_KEY_STATUS_KS_VALID_INTERFACE_MASK 0x7f
#define SEP_KEY_BASE_KEY_STATUS_KS_VALID_INTERFACE_GET(n)        \
    (((n) >> SEP_KEY_BASE_KEY_STATUS_KS_VALID_INTERFACE_SHIFT) & \
     SEP_KEY_BASE_KEY_STATUS_KS_VALID_INTERFACE_MASK)
#define SEP_KEY_BASE_KEY_STATUS_KS_VALID_INTERFACE_SET(n)    \
    (((n) & SEP_KEY_BASE_KEY_STATUS_KS_VALID_INTERFACE_MASK) \
     << SEP_KEY_BASE_KEY_STATUS_KS_VALID_INTERFACE_SHIFT)

// for KEY_BASE register 0x4: load key
// interface value is a bitmask
#define SEP_KEY_BASE_LOAD_KEY_OFFSET 0x4
#define SEP_KEY_BASE_LOAD_KEY_INTERFACE_SHIFT 8
#define SEP_KEY_BASE_LOAD_KEY_INTERFACE_MASK 0xff
#define SEP_KEY_BASE_LOAD_KEY_INTERFACE_GET(n)        \
    (((n) >> SEP_KEY_BASE_LOAD_KEY_INTERFACE_SHIFT) & \
     SEP_KEY_BASE_LOAD_KEY_INTERFACE_MASK)
#define SEP_KEY_BASE_LOAD_KEY_INTERFACE_SET(n)    \
    (((n) & SEP_KEY_BASE_LOAD_KEY_INTERFACE_MASK) \
     << SEP_KEY_BASE_LOAD_KEY_INTERFACE_SHIFT)

// unkn0 0x0==Lc128/DpkTx, 0x1/0x3==Km/Ks, 0x2==Km
#define SEP_KEY_BASE_LOAD_KEY_UNKN0_SHIFT 4
#define SEP_KEY_BASE_LOAD_KEY_UNKN0_MASK 0x3
#define SEP_KEY_BASE_LOAD_KEY_UNKN0_GET(n)        \
    (((n) >> SEP_KEY_BASE_LOAD_KEY_UNKN0_SHIFT) & \
     SEP_KEY_BASE_LOAD_KEY_UNKN0_MASK)
#define SEP_KEY_BASE_LOAD_KEY_UNKN0_SET(n)    \
    (((n) & SEP_KEY_BASE_LOAD_KEY_UNKN0_MASK) \
     << SEP_KEY_BASE_LOAD_KEY_UNKN0_SHIFT)

#define SEP_KEY_BASE_LOAD_KEY_ACTIVE BIT(0)

// for KEY_BASE registers 0xc (PKA) && 0x10 (AESH) && 0x14 (AES2): send key
// die offset/bitshift is unknown
// interface value is a bitmask
// interface is for Km for PKA && AESH, but Ks for AES2
#define SEP_KEY_BASE_SEND_KEY_PKA_OFFSET 0xc
#define SEP_KEY_BASE_SEND_KEY_AESH_OFFSET 0x10
#define SEP_KEY_BASE_SEND_KEY_AES2_OFFSET 0x14
#define SEP_KEY_BASE_SEND_KEY_INTERFACE_SHIFT 8
#define SEP_KEY_BASE_SEND_KEY_INTERFACE_MASK 0x7
#define SEP_KEY_BASE_SEND_KEY_INTERFACE_GET(n)        \
    (((n) >> SEP_KEY_BASE_SEND_KEY_INTERFACE_SHIFT) & \
     SEP_KEY_BASE_SEND_KEY_INTERFACE_MASK)
#define SEP_KEY_BASE_SEND_KEY_INTERFACE_SET(n)    \
    (((n) & SEP_KEY_BASE_SEND_KEY_INTERFACE_MASK) \
     << SEP_KEY_BASE_SEND_KEY_INTERFACE_SHIFT)
#define SEP_KEY_BASE_SEND_KEY_ACTIVE BIT(0)

// handler key selectors: Lc128==0x0; DpkTx==0x1; Km==0x2; Ks==0x3

#define SEP_PMGR_REGISTER_POWER_CONTROL 0x4000
#define SEP_PMGR_REGISTER_POWER_CONTROL_POWER_GATE_DELAY_SHIFT 16
#define SEP_PMGR_REGISTER_ACG_CONTROL 0x4004 // some tunables shit


static inline void block16_set(union nettle_block16 *r,
                               const union nettle_block16 *x)
{
    r->u64[0] = x->u64[0];
    r->u64[1] = x->u64[1];
}
static void drbg_ctr_aes256_output(const struct aes256_ctx *key,
                                   union nettle_block16 *V, size_t n,
                                   uint8_t *dst)
{
    for (; n >= AES_BLOCK_SIZE; n -= AES_BLOCK_SIZE, dst += AES_BLOCK_SIZE) {
        INCREMENT(AES_BLOCK_SIZE, V->b);
        aes256_encrypt(key, AES_BLOCK_SIZE, dst, V->b);
    }
    if (n > 0) {
        union nettle_block16 block;

        INCREMENT(AES_BLOCK_SIZE, V->b);
        aes256_encrypt(key, AES_BLOCK_SIZE, block.b, V->b);
        memcpy(dst, block.b, n);
    }
}
static void drbg_ctr_aes256_update(struct aes256_ctx *key,
                                   union nettle_block16 *V,
                                   const uint8_t *provided_data)
{
    union nettle_block16 tmp[3];
    drbg_ctr_aes256_output(key, V, DRBG_CTR_AES256_SEED_SIZE, tmp[0].b);

    if (provided_data) {
        memxor(tmp[0].b, provided_data, DRBG_CTR_AES256_SEED_SIZE);
    }

    aes256_set_encrypt_key(key, tmp[0].b);
    block16_set(V, &tmp[2]);
}

static void dump_cpu_handler(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    AppleSEPState *sep = APPLE_SEP(
        object_property_get_link(OBJECT(machine), "sep", &error_fatal));
    g_assert_nonnull(sep);
    cpu_dump_state(CPU(sep->cpu), stderr, CPU_DUMP_CODE);
}

static void enable_trace_buffer(AppleSEPState *s)
{
    DPRINTF("SEP_PROGRESS: Enable Trace Buffer: s->shmbuf_base: "
            "0x" HWADDR_FMT_plx "\n",
            s->shmbuf_base);
    if (!s->shmbuf_base) {
        return;
    }
    AddressSpace *nsas = &address_space_memory;
    typedef struct {
        uint32_t name;
        uint32_t size;
        uint64_t offset;
    } QEMU_PACKED shm_region_t;
#ifdef SEP_ENABLE_OVERWRITE_SHMBUF_OBJECTS
    shm_region_t shm_region_TRAC = { 0 };
    g_assert_cmpuint(sizeof(shm_region_TRAC), ==, 0x10);
    shm_region_TRAC.name = 'TRAC';
    shm_region_TRAC.size = s->debug_trace_size;
    shm_region_TRAC.offset = s->trace_buffer_base_offset;
    shm_region_t shm_region_null = { 0 };
    g_assert_cmpuint(sizeof(shm_region_null), ==, 0x10);
    shm_region_null.name = 'null';
    uint32_t region_SCOT_size = 0x4000;
    address_space_write(nsas, s->shmbuf_base + 0x14, MEMTXATTRS_UNSPECIFIED,
                        &region_SCOT_size, sizeof(region_SCOT_size));
    address_space_write(nsas, s->shmbuf_base + 0x20, MEMTXATTRS_UNSPECIFIED,
                        &shm_region_TRAC, sizeof(shm_region_TRAC));
    address_space_write(nsas, s->shmbuf_base + 0x30, MEMTXATTRS_UNSPECIFIED,
                        &shm_region_null, sizeof(shm_region_null));
    address_space_set(nsas, s->shmbuf_base + 0xC000 + 0x20, 0,
                      region_SCOT_size - 0x20,
                      MEMTXATTRS_UNSPECIFIED); // clean up SCOT a bit
#endif
    typedef struct {
        uint64_t name;
        uint64_t size;
        uint8_t maybe_permissions; // 0x04/0x06/0x16 // (arg5 & 1) != 0
                                   // create_object panic? ;; maybe permissions
        uint8_t arg6; // 0x00/0x02/0x06 // >= 0x03 create_object panic?
        uint8_t arg7; // 0x01/0x02/0x03/0x04/0x05/0x0D/0x0E/0x0F/0x10 // if
                      // (arg7 != 0) create_object data_346d0 checking block ;;
                      // maybe module_index
        uint8_t pad0;
        uint32_t unkn1; // maybe segment name like _dat, _asc, STAK, TEXT, PMGR
                        // or _hep.
        uint64_t phys;
        uint32_t phys_module_name; // phys module name like EISP
        uint32_t phys_region_name; // phys region name like BASE
        uint64_t virt_mapping_next; // sepos_virt_mapping_t
        uint64_t virt_mapping_previous; // sepos_virt_mapping_t.next or
                                        // object_mappings_t.virt_mapping_next
        uint64_t acl_next; // sepos_acl_t
        uint64_t acl_previous; // sepos_acl_t.next or object_mappings_t.acl_next
    } QEMU_PACKED object_mappings_t;
    typedef struct {
        uint64_t object_mapping; // object_mappings_t
        uint64_t maybe_virt_base;
        uint8_t sending_pid;
        uint8_t maybe_permissions; // maybe permissions ;; data0
        uint8_t maybe_subregion; // 0x00/0x01/0x02 ;; data1
        uint8_t pad0;
        uint32_t pad1;
        uint64_t module_next; // sepos_virt_mapping_t
        uint64_t module_previous; // sepos_virt_mapping_t.next
        uint64_t all_next; // sepos_virt_mapping_t
        uint64_t all_previous; // sepos_virt_mapping_t.all_next
    } QEMU_PACKED sepos_virt_mapping_t;
    typedef struct {
        uint32_t maybe_module_id; // 0x2/0x3/0x4/10001
        uint32_t acl; // 0x4/0x6/0x14/0x16
        uint64_t next; // sepos_acl_t
        uint64_t previous; // sepos_acl_t.next
    } QEMU_PACKED sepos_acl_t;
    object_mappings_t object_mapping_TRAC = { 0 };
    g_assert_cmpuint(sizeof(object_mapping_TRAC), ==, 0x48);
    sepos_acl_t acl_for_TRAC = { 0 };
    g_assert_cmpuint(sizeof(acl_for_TRAC), ==, 0x18);
    sepos_virt_mapping_t virt_mapping_for_TRAC = { 0 };
    g_assert_cmpuint(sizeof(virt_mapping_for_TRAC), ==, 0x38);

// SEPOS_PHYS_BASEs: not in runtime, but while in SEPROM. Same on T8020
// (0x340611BA8-0x11BA8)
// get this with gdb, prerequisite is disabling aslr(?):
// b *0x<sepos_module_start_function> ; gva2gpa 0x<sepos_module_start_function>
// the result minus <sepos_module_start_function from binja without rebasing>
// &~0x100000000 (only if the upper bits in the sepos address are set?)
// xp/1xw phys_base + 0x8000 should be 0xfeedfacf
// maybe it's not that easy to disable the SEPOS module ASLR under iOS 15:
// so instead make breakpoints for the second (or both) eret and do "si".
// disabling the SEPOS module's ASLR was easy under iOS 16.
#define SEPOS_PHYS_BASE_T8015 (0x3404A4000ULL)
#define SEPOS_PHYS_BASE_T8020_IOS14 (0x340600000ULL)
#define SEPOS_PHYS_BASE_T8020_IOS15 (0x340710000ULL)
// #define SEPOS_PHYS_BASE_T8030_IOS14 (0x340634000ULL) // for 14.7.1
#define SEPOS_PHYS_BASE_T8030_IOS14 (0x340628000ULL) // for 14beta5
#define SEPOS_PHYS_BASE_T8030_IOS15 (0x34075C000ULL)
#define SEPOS_PHYS_BASE_T8030_IOS16 (0x340440000ULL)
#define SEPOS_PHYS_BASE_T8030_IOS18 (0x3403a0000ULL)
// for T8020/T8030 SEPFW of early 14 and 14.7.1
#define SEPOS_OBJECT_MAPPING_BASE_VERSION_IOS14 (0x198D0)
#define SEPOS_OBJECT_MAPPING_BASE_VERSION_IOS15 (0x1D748)
#define SEPOS_OBJECT_MAPPING_BASE_VERSION_IOS16 (0x398b0)
#define SEPOS_OBJECT_MAPPING_BASE_VERSION_IOS18 (0x298b0)
#define SEPOS_OBJECT_MAPPING_INDEX (7)
// #define SEPOS_VIRT_MAPPING_BASE 0x282D0
// #define SEPOS_VIRT_MAPPING_INDEX 555
// for T8020/T8030 SEPFW of early 14 and 14.7.1
#define SEPOS_ACL_BASE_VERSION_IOS14 (0x140D0)
#define SEPOS_ACL_BASE_VERSION_IOS15 (0x18348)
#define SEPOS_ACL_BASE_VERSION_IOS16 (0x2c000)
#define SEPOS_ACL_BASE_VERSION_IOS18 (0x1c000)
#define SEPOS_ACL_INDEX (19)

    uint64_t sepos_phys_base = 0x0;
    uint64_t sepos_object_mapping_base = 0x0;
    uint64_t sepos_acl_base = 0x0;
#if SEP_USE_VERSION_OVERRIDE == 14
    sepos_object_mapping_base = SEPOS_OBJECT_MAPPING_BASE_VERSION_IOS14;
    sepos_acl_base = SEPOS_ACL_BASE_VERSION_IOS14;
#elif SEP_USE_VERSION_OVERRIDE == 15
    sepos_object_mapping_base = SEPOS_OBJECT_MAPPING_BASE_VERSION_IOS15;
    sepos_acl_base = SEPOS_ACL_BASE_VERSION_IOS15;
#elif SEP_USE_VERSION_OVERRIDE == 16
    sepos_object_mapping_base = SEPOS_OBJECT_MAPPING_BASE_VERSION_IOS16;
    sepos_acl_base = SEPOS_ACL_BASE_VERSION_IOS16;
#elif SEP_USE_VERSION_OVERRIDE == 18
    sepos_object_mapping_base = SEPOS_OBJECT_MAPPING_BASE_VERSION_IOS18;
    sepos_acl_base = SEPOS_ACL_BASE_VERSION_IOS18;
#endif
    if (s->chip_id == 0x8015) {
        sepos_phys_base = SEPOS_PHYS_BASE_T8015;
    } else if (s->chip_id == 0x8020) {
#if SEP_USE_VERSION_OVERRIDE == 14
        sepos_phys_base = SEPOS_PHYS_BASE_T8020_IOS14;
#elif SEP_USE_VERSION_OVERRIDE == 15
        sepos_phys_base = SEPOS_PHYS_BASE_T8020_IOS15;
#elif SEP_USE_VERSION_OVERRIDE == 16
        g_assert_not_reached();
#elif SEP_USE_VERSION_OVERRIDE == 18
        g_assert_not_reached();
#endif
    } else if (s->chip_id == 0x8030) {
#if SEP_USE_VERSION_OVERRIDE == 14
        sepos_phys_base = SEPOS_PHYS_BASE_T8030_IOS14;
#elif SEP_USE_VERSION_OVERRIDE == 15
        sepos_phys_base = SEPOS_PHYS_BASE_T8030_IOS15;
#elif SEP_USE_VERSION_OVERRIDE == 16
        sepos_phys_base = SEPOS_PHYS_BASE_T8030_IOS16;
#elif SEP_USE_VERSION_OVERRIDE == 18
        sepos_phys_base = SEPOS_PHYS_BASE_T8030_IOS18;
#endif
    } else {
        g_assert_not_reached();
    }
#if SEP_USE_VERSION_OVERRIDE == 14
    object_mapping_TRAC.name = 'TRAC';
    object_mapping_TRAC.size = s->debug_trace_size;
    object_mapping_TRAC.maybe_permissions = 0x06;
    object_mapping_TRAC.arg6 = 0x00;
    object_mapping_TRAC.arg7 = 0x01;
    object_mapping_TRAC.unkn1 = '_dat';
    object_mapping_TRAC.phys = s->shmbuf_base + s->trace_buffer_base_offset;
    object_mapping_TRAC.virt_mapping_previous =
        sepos_object_mapping_base +
        (sizeof(object_mappings_t) * SEPOS_OBJECT_MAPPING_INDEX) +
        offsetof(object_mappings_t, virt_mapping_next);
    // object_mapping_TRAC.virt_mapping_next = SEPOS_VIRT_MAPPING_BASE +
    // (sizeof(sepos_virt_mapping_t) * SEPOS_VIRT_MAPPING_INDEX);
    // object_mapping_TRAC.virt_mapping_previous = SEPOS_VIRT_MAPPING_BASE +
    // (sizeof(sepos_virt_mapping_t) * SEPOS_VIRT_MAPPING_INDEX) +
    // offsetof(sepos_virt_mapping_t, module_next);
    object_mapping_TRAC.acl_next =
        sepos_acl_base + (sizeof(sepos_acl_t) * SEPOS_ACL_INDEX);
    object_mapping_TRAC.acl_previous = sepos_acl_base +
                                       (sizeof(sepos_acl_t) * SEPOS_ACL_INDEX) +
                                       offsetof(sepos_acl_t, next);
    address_space_write(
        nsas,
        sepos_phys_base + sepos_object_mapping_base +
            (sizeof(object_mappings_t) * SEPOS_OBJECT_MAPPING_INDEX),
        MEMTXATTRS_UNSPECIFIED, &object_mapping_TRAC,
        sizeof(object_mapping_TRAC));
    acl_for_TRAC.maybe_module_id = 10001;
    ////acl_for_TRAC.maybe_module_id = 55; // non-existant
    acl_for_TRAC.acl = 0x6;
    acl_for_TRAC.previous =
        sepos_object_mapping_base +
        (sizeof(object_mappings_t) * SEPOS_OBJECT_MAPPING_INDEX) +
        offsetof(object_mappings_t, acl_next);
    address_space_write(nsas,
                        sepos_phys_base + sepos_acl_base +
                            (sizeof(sepos_acl_t) * SEPOS_ACL_INDEX),
                        MEMTXATTRS_UNSPECIFIED, &acl_for_TRAC,
                        sizeof(acl_for_TRAC));
#endif
    // alternative bypass as if_module_AAES_Debu_or_SEPD is also used by other
    // functions, more restrictive.
    uint32_t value32_nop = 0xD503201F; // nop
    uint64_t bypass_offset = 0;
    if (s->chip_id == 0x8020) {
#if SEP_USE_VERSION_OVERRIDE == 14
        bypass_offset = 0x11BB0; // T8020 iOS14
#elif SEP_USE_VERSION_OVERRIDE == 15
        bypass_offset = 0x12FB4; // T8020 iOS15
#endif
    } else if (s->chip_id == 0x8030) {
#if SEP_USE_VERSION_OVERRIDE == 14
        // bypass_offset = 0x11B34; // T8030 iOS14.7.1
        bypass_offset = 0x11C38; // T8030 iOS14beta5
#elif SEP_USE_VERSION_OVERRIDE == 15
        bypass_offset = 0x12E9C; // T8030 iOS15
#elif SEP_USE_VERSION_OVERRIDE == 16
        bypass_offset = 0x1A074; // T8030 iOS16
#elif SEP_USE_VERSION_OVERRIDE == 18
        bypass_offset = 0x14db4; // T8030 iOS18
#endif
    } else if (s->chip_id == 0x8015) {
        // T8015's SEPFW SEPOS is not reachable from SEPROM, it's LZVN
        // compressed.
        bypass_offset = 0x11C2C; // T8015
    }
    address_space_write(nsas, sepos_phys_base + bypass_offset,
                        MEMTXATTRS_UNSPECIFIED, &value32_nop,
                        sizeof(value32_nop));
}

static void disable_aslr(AppleSEPState *s)
{
    DPRINTF("SEP_PROGRESS: Disable ASLR\n");
    AddressSpace *nsas = &address_space_memory;

    hwaddr phys_addr = 0x0;
    // easy way of retrieving the sepb random_0 address
    // T8020: b *0x340000000 ; p/x $x0+0x80 == e.g. 0x340736380
    // easy way of retrieving the sepb random_0 address
    // T8030: go to the first SYS_ACC_PWR_DN_SAVE read in the kernel,
    // and then do p/x $x0+0x80 == e.g. 0x3407CA380
    if (s->chip_id == 0x8015) {
#if SEP_USE_VERSION_OVERRIDE == 14
        phys_addr = 0x34015FD40ULL; // T8015
#else
        g_assert_not_reached();
#endif
    } else if (s->chip_id == 0x8020) {
#if SEP_USE_VERSION_OVERRIDE == 14
        phys_addr = 0x340736380ULL; // T8020 iOS 14
#elif SEP_USE_VERSION_OVERRIDE == 15
        phys_addr = 0x34086E380ULL; // T8020 iOS 15
#elif SEP_USE_VERSION_OVERRIDE == 16
        g_assert_not_reached();
#elif SEP_USE_VERSION_OVERRIDE == 18
        g_assert_not_reached();
#endif
    } else if (s->chip_id == 0x8030) {
#if SEP_USE_VERSION_OVERRIDE == 14
        // phys_addr = 0x3407CA380ULL; // T8030 iOS 14.7.1
        phys_addr = 0x34076E380ULL; // T8030 iOS 14beta5
#elif SEP_USE_VERSION_OVERRIDE == 15
        phys_addr = 0x34090A380ULL; // T8030 iOS 15
#elif SEP_USE_VERSION_OVERRIDE == 16
        phys_addr = 0x3406de380ULL; // T8030 iOS 16
#elif SEP_USE_VERSION_OVERRIDE == 18
        phys_addr = 0x340626380ULL; // T8030 iOS 18
#endif
    } else {
        g_assert_not_reached();
    }
    if (phys_addr) {
        // The first 16bytes of SEPB.random_0 are being used for SEPOS'
        // ASLR. GDB's awatch refuses to tell me where it ends up, so
        // here you go, I'm just zeroing that shit. == This disables
        // ASLR for SEPOS apps
        // Future iOS versions might use more than 16 bytes, so zero
        // the whole field here.
        // phys_SEPB + 0x80; pc==0x240005BAC
        address_space_set(nsas, phys_addr, 0, 0x40, MEMTXATTRS_UNSPECIFIED);
    }
}

static const char *
sepos_return_module_thread_string_t8015(uint64_t module_thread_id)
{
    // base == sepdump02_SEPOS?
    // T8015 thread name/info base 0xFFFFFFE00001A988

    switch (module_thread_id) {
    case 0x0:
        return "SEPOS"; // SEPOS/BOOT, actually BOOT
    case 0x10000:
        return "SEPD";
    case 0x10001:
        return "intr";
    case 0x10002:
        return "XPRT";
    case 0x10003:
        return "PMGR";
    case 0x10004:
        return "AKF";
    case 0x10005:
        return "EP0D";
    case 0x10006:
        return "TRNG";
    case 0x10007:
        return "KEY";
    case 0x10008:
        return "shnd";
    case 0x10009:
        return "ep0";
    case 0x20000:
        return "DAES";
    case 0x20001:
        return "AESS";
    case 0x20002:
        return "AEST";
    case 0x20003:
        return "PKA";
    case 0x30000:
        return "dxio";
    case 0x30001:
        return "GPIO";
    case 0x30002:
        return "I2C";
    case 0x40000:
        return "enti";
    case 0x50000:
        return "sskg";
    case 0x50001:
        return "skgs";
    case 0x50002:
        return "crow";
    case 0x50003:
        return "cro2";
    case 0x60000:
        return "sars";
    case 0x70000:
        return "ARTM";
    case 0x80000:
        return "xART";
    case 0x90000:
        return "scrd";
    case 0xA0000:
        return "pass";
    case 0xB0000:
        return "sks"; // 13
    case 0xB0001:
        return "sksa";
    case 0xC0000:
        return "sbio"; // 14
    case 0xC0001:
        return "SBIO_THREAD"; // thread name missing from array
    case 0xD0000:
        return "sse"; // 15
    default:
        return "Unknown";
    }
}

static const char *
sepos_return_module_thread_string_t8030(uint64_t module_thread_id)
{
    // base == sepdump02_SEPOS?
    // T8020/T8030 thread name/info base 0xFFFFFFE00001B1C8

    switch (module_thread_id) {
    case 0x0:
        return "BOOT"; // SEPOS
    case 0x10000:
        return "SEPD";
    case 0x10001:
        return "intr";
    case 0x10002:
        return "XPRT";
    case 0x10003:
        return "PMGR";
    case 0x10004:
        return "AKF";
    case 0x10005:
        return "EP0D";
    case 0x10006:
        return "TRNG";
    case 0x10007:
        return "KEY";
    case 0x10008:
        return "MONI";
    case 0x10009:
        return "AESH";
    case 0x1000A:
        return "EISP";
    case 0x1000B:
        return "shnd";
    case 0x1000C:
        return "ep0";
    case 0x20000:
        return "DAES";
    case 0x20001:
        return "AESS";
    case 0x20002:
        return "AEST";
    case 0x20003:
        return "PKA";
    case 0x30000:
        return "dxio";
    case 0x30001:
        return "GPIO";
    case 0x30002:
        return "I2C";
    case 0x40000:
        return "enti";
    case 0x50000:
        return "sskg";
    case 0x50001:
        return "skgs";
    case 0x50002:
        return "crow";
    case 0x50003:
        return "cro2";
    case 0x60000:
        return "sars";
    case 0x70000:
        return "ARTM";
    case 0x80000:
        return "xART";
    case 0x90000:
        return "eiAp";
    case 0x90001:
        return "EISP";
    case 0x90002:
        return "HWRS";
    case 0x90003:
        return "FDCN";
    case 0x90004:
        return "SDCN";
    case 0x90005:
        return "FIPP";
    case 0x90006:
        return "FPCE";
    case 0x90007:
        return "FPPD";
    case 0x90008:
        return "FDMA";
    case 0x90009:
        return "SHAV";
    case 0x9000A:
        return "PROX";
    case 0xA0000:
        return "scrd";
    case 0xB0000:
        return "pass";
    case 0xC0000:
        return "sks";
    case 0xC0001:
        return "sksa";
    case 0xD0000:
        return "hdcp";
    case 0xE0000:
        return "sprl";
    case 0xF0000:
        return "sse";
    default:
        return "Unknown";
    }
}

static const char *
sepos_return_module_thread_string_t8030_15(uint64_t module_thread_id)
{
    // base == sepdump02_SEPOS?
    // T8030 sepfw 15 thread name/info base 0xFFFFFFE0000235C8

    switch (module_thread_id) {
    case 0x0:
        return "BOOT"; // SEPOS
    case 0x10000:
        return "SEPD";
    case 0x10001:
        return "intr";
    case 0x10002:
        return "Cons";
    case 0x10003:
        return "XPRT";
    case 0x10004:
        return "PMGR";
    case 0x10005:
        return "AKF ";
    case 0x10006:
        return "EP0D";
    case 0x10007:
        return "EPCD";
    case 0x10008:
        return "TRNG";
    case 0x10009:
        return "KEY ";
    case 0x1000A:
        return "MONI";
    case 0x1000B:
        return "AESH";
    case 0x1000C:
        return "EISP";
    case 0x1000D:
        return "cnin";
    case 0x1000E:
        return "shnd";
    case 0x1000F:
        return "ep0 ";
    case 0x10010:
        return "ep1 ";
    case 0x20000:
        return "DAES";
    case 0x20001:
        return "AESS";
    case 0x20002:
        return "AEST";
    case 0x20003:
        return "PKA ";
    case 0x30000:
        return "dxio";
    case 0x30001:
        return "GPIO";
    case 0x30002:
        return "I2C ";
    case 0x40000:
        return "enti";
    case 0x50000:
        return "sskg";
    case 0x50001:
        return "skgs";
    case 0x50002:
        return "crow";
    case 0x50003:
        return "cro2";
    case 0x60000:
        return "sars";
    case 0x70000:
        return "ARTM";
    case 0x80000:
        return "xART";
    case 0x90000:
        return "eiAp";
    case 0x90001:
        return "EISP";
    case 0x90002:
        return "HWRS";
    case 0x90003:
        return "FDCN";
    case 0x90004:
        return "SDCN";
    case 0x90005:
        return "FIPP";
    case 0x90006:
        return "FPCE";
    case 0x90007:
        return "FPPD";
    case 0x90008:
        return "FDMA";
    case 0x90009:
        return "SHAV";
    case 0x9000A:
        return "PROX";
    case 0xA0000:
        return "scrd";
    case 0xB0000:
        return "pass";
    case 0xC0000:
        return "sks ";
    case 0xC0001:
        return "sksa";
    case 0xD0000:
        return "hdcp";
    case 0xE0000:
        return "pair";
    case 0xF0000:
        return "sprl";
    case 0x100000:
        return "sse";
    case 0x110000:
        return "sidv";
    case 0x120000:
        return "unit";
    case 0x1D1E1D1E:
        return "IDLE";
    default:
        return "Unknown";
    }
}

static const char *sepos_return_module_thread_string(uint32_t chip_id,
                                                     uint64_t module_thread_id)
{
    if (chip_id == 0x8015) {
        return sepos_return_module_thread_string_t8015(module_thread_id);
    } else if (chip_id == 0x8030) {
#if SEP_USE_VERSION_OVERRIDE == 15
        return sepos_return_module_thread_string_t8030_15(module_thread_id);
#else
        return sepos_return_module_thread_string_t8030(module_thread_id);
#endif
    }
    g_assert_not_reached();
}

static void debug_trace_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                  unsigned size)
{
    AppleSEPState *s = opaque;
    uint32_t offset = 0;
    if (size == 1) {
        // iOS 15 SEPFW workaround against a brief logspam
        return;
    }

#ifdef ENABLE_CPU_DUMP_STATE
    // cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif

    if (s->shmbuf_base == 0) {
        qemu_log_mask(
            LOG_UNIMP,
            "DEBUG_TRACE: SHMBUF_BASE==NULL: Unknown write at 0x" HWADDR_FMT_plx
            " of value 0x%" PRIX64 " size=%u\n",
            addr, data, size);
        return;
    }

    offset = ((uint32_t *)s->debug_trace_regs)[0x4 / 4];
    if (offset != 0) {
        offset -= 1;
        offset <<= 6;
    }

    memcpy(&s->debug_trace_regs[addr], &data, size);

    uint32_t addr_mod = addr % 0x40;
    if (addr != 0x40 && // offset register
        addr != 0x04 && // some index
        addr_mod != 0x20 && addr_mod != 0x28 && addr_mod != 0x00 &&
        addr_mod != 0x08 && addr_mod != 0x10 && addr_mod != 0x18 &&
        addr_mod != 0x30) {
        qemu_log_mask(LOG_UNIMP,
                      "DEBUG_TRACE: Unknown write at 0x" HWADDR_FMT_plx
                      " of value 0x%" PRIX64 " size=%u offset==0x%08x\n",
                      addr, data, size, offset);
    }

    // Might not include SEPOS output, as it's not initialized like e.g.
    // SEPD.
    if (addr_mod != 0x30) {
        return;
    }

    SEPMessage m = { 0 };
    uint64_t trace_id = *(uint64_t *)&s->debug_trace_regs[addr - 0x30];
    uint64_t arg2 = *(uint64_t *)&s->debug_trace_regs[addr - 0x28];
    uint64_t arg3 = *(uint64_t *)&s->debug_trace_regs[addr - 0x20];
    uint64_t arg4 = *(uint64_t *)&s->debug_trace_regs[addr - 0x18];
    uint64_t arg5 = *(uint64_t *)&s->debug_trace_regs[addr - 0x10];
    uint64_t tid = *(uint64_t *)&s->debug_trace_regs[addr - 0x08];
    uint64_t time = *(uint64_t *)&s->debug_trace_regs[addr - 0x00];
    DPRINTF("\nDEBUG_TRACE: Debug:"
            " 0x%" PRIX64 " 0x%" PRIX64 " 0x%" PRIX64 " 0x%" PRIX64
            " 0x%" PRIX64 " 0x%" PRIX64 " %" PRIu64 "\n",
            trace_id, arg2, arg3, arg4, arg5, tid, time);
    const char *tid_str = sepos_return_module_thread_string(s->chip_id, tid);
    switch (trace_id) {
    case 0x82000004: { // SEP L4 task switch
        // %s instead of %c%c%c%c because the names will be nullbytes sometimes.
        uint64_t old_taskname = bswap32(arg2);
        uint64_t new_taskname = bswap32(arg4);
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: SEP "
                "L4 task switch: old task thread name: 0x%02" PRIX64
                "(%s) old task id: 0x%05" PRIX64
                " new task thread name: 0x%02" PRIX64 "(%s) "
                "arg5: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, (char *)&old_taskname, arg3, arg4,
                (char *)&new_taskname, arg5);
        break;
    }
    case 0x82010004: // panic
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: SEP "
                "module panicked\n",
                tid, tid_str);
        break;
    case 0x82030004: // initialize_ool_page
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "initialize_ool_page:"
                " obj_id: 0x%02" PRIX64 " address: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3);
        break;
    case 0x82040005: // before SEP_IO__Control
    case 0x82040006: // after SEP_IO__Control
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: %s "
                "SEP_IO__Control Sending message to other module:"
                " fromto: 0x%02" PRIX64 " method: 0x%02" PRIX64
                " data0: 0x%02" PRIX64 " "
                "data1: 0x%02" PRIX64 "\n",
                tid, tid_str, (trace_id == 0x82040005) ? "Before" : "After",
                arg2, arg3, arg4, arg5);
        break;
    case 0x82050005: // SEP_SERVICE__Call: request
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_SERVICE__Call: request:"
                " fromto: 0x%02" PRIX64 " interface_msgid: 0x%02" PRIX64 " "
                "method: 0x%02" PRIX64 " data0: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82050006: // SEP_SERVICE__Call: response
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_SERVICE__Call: response:"
                " fromto: 0x%02" PRIX64 " interface_msgid: 0x%02" PRIX64 " "
                "method: 0x%02" PRIX64 " status/data0: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82060004: // entered workloop function
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: SEP "
                "module entered workloop function:"
                " handlers0: 0x%02" PRIX64 " handlers1: 0x%02" PRIX64 " arg5: "
                "0x%02" PRIX64 " arg6: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82060010: // workloop function: interface_msgid==0xFFFE after
                     // receiving
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: SEP "
                "module workloop function:"
                " interface_msgid==0xFFFE after receiving: "
                "data0: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2);
        break;
    case 0x82060014: // workloop function: before handlers0 handler
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: SEP module "
                "workloop function: before handlers0 handler:"
                " handler_index: 0x%02" PRIX64 " data0: 0x%02" PRIX64
                " data1: 0x%02" PRIX64 " "
                "data2: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82060018: // workloop function: handlers0: handler not found,
                     // panic
        DPRINTF(

            "DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: SEP module "
            "workloop function: handlers0: handler not found, panic:"
            " interface_msgid: 0x%02" PRIX64 " method: 0x%02" PRIX64 " data0: "
            "0x%02" PRIX64 " "
            "data1: 0x%02" PRIX64 "\n",
            tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x8206001C: // workloop function: interface_msgid==0xFFFE
                     // before handler
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: SEP "
                "module workloop function:"
                " interface_msgid==0xFFFE before handler: data0: "
                "0x%02" PRIX64 " handler: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3);
        break;
    case 0x82080005: // 0x82080005==before Rpc_Call
    case 0x82080006: // 0x82080006==after Rpc_Call
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: %s "
                "Rpc_Call Sending message to other module:"
                " fromto: 0x%02" PRIX64 " interface_msgid: 0x%02" PRIX64
                " ool: 0x%02" PRIX64 " method: 0x%02" PRIX64 "\n",
                tid, tid_str, (trace_id == 0x82080005) ? "Before" : "After",
                arg2, arg3, arg4, arg5);
        break;
    case 0x8208000D: // before Rpc_Wait
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: Before "
                "Rpc_Wait Receiving message from other module\n",
                tid, tid_str);
        break;
    case 0x8208000E: // after Rpc_Wait
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: After "
                "Rpc_Wait "
                "Receiving message from other module:"
                " fromto: 0x%02" PRIX64 " interface_msgid: 0x%02" PRIX64
                " ool: 0x%02" PRIX64 " method: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82080019: // before Rpc_WaitFrom
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: Before "
                "Rpc_WaitFrom Receiving message from other module:"
                " arg2: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2);
        break;
    case 0x8208001A: // after Rpc_WaitFrom
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: After "
                "Rpc_WaitFrom Receiving message from other module:"
                " fromto: 0x%02" PRIX64 " interface_msgid: 0x%02" PRIX64
                " ool: 0x%02" PRIX64 " method: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82080011: // before Rpc_ReturnWait
    case 0x82080012: // after Rpc_ReturnWait
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: %s "
                "Rpc_ReturnWait Receiving message from other module:"
                " fromto: 0x%02" PRIX64 " interface_msgid: 0x%02" PRIX64
                " ool: 0x%02" PRIX64 " method: 0x%02" PRIX64 "\n",
                tid, tid_str, (trace_id == 0x82080011) ? "Before" : "After",
                arg2, arg3, arg4, arg5);
        break;
    case 0x82080014: // before Rpc_Return return response
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "Before Rpc_Return return response:"
                " fromto: 0x%02" PRIX64 " interface_msgid: 0x%02" PRIX64
                " ool: 0x%02" PRIX64 " method: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x8208001D: // before Rpc_WaitNotify
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: Before "
                "Rpc_WaitNotify:"
                " Rpc_WaitNotify_arg2 != 0: Rpc_WaitNotify_arg1: "
                "0x%02" PRIX64 "\n",
                tid, tid_str, arg2);
        break;
    case 0x8208001E: // after Rpc_WaitNotify
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "After Rpc_WaitNotify:"
                " svc_0x5_0_func_arg2 != 0: svc_0x5_0_func_arg1: "
                "0x%02" PRIX64 " L4_MR0: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3);
        break;
    case 0x82140004: // _dispatch_thread_main__intr/SEPD interrupt
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "_dispatch_thread_main__intr/SEPD interrupt "
                "trace_id 0x%02" PRIX64 ":"
                " arg2: 0x%02" PRIX64 " arg3: 0x%02" PRIX64
                " arg4: 0x%02" PRIX64 " arg5: 0x%02" PRIX64 "\n",
                tid, tid_str, trace_id, arg2, arg3, arg4, arg5);
        break;
    case 0x82140014: // SEP_Driver__Close
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Close:"
                " module_name_int: 0x%02" PRIX64 " fromto: 0x%02" PRIX64 " "
                "response_data0: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg5);
        break;
    case 0x82140024: // *_enable_powersave_arg2/SEP_Driver__SetPowerState
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__SetPowerState:"
                " function called: enable_powersave?: 0x%02" PRIX64 " "
                "is_powersave_enabled: 0x%02" PRIX64 " field_cc3: 0x%02" PRIX64
                "\n",
                tid, tid_str, arg2, arg3, arg4);
        break;
    case 0x82140031: // SEPD_thread_handler:
                     // SEP_Driver__before_InterruptAsync
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEPD_thread_handler: before_InterruptAsync:"
                " arg2: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2);
        break;
    case 0x82140032: // SEPD_thread_handler:
                     // SEP_Driver__after_InterruptAsync
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEPD_thread_handler: after_InterruptAsync\n",
                tid, tid_str);
        break;
    case 0x82140195: // AESS_message_received: before
                     // AESS_keywrap_cmd_0x02
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "AESS_message_received: before AESS_keywrap_cmd_0x02:"
                " data0_low: 0x%02" PRIX64 " data0_high: 0x%02" PRIX64
                " data1_low: "
                "0x%02" PRIX64 " data1_high: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82140196: // AESS_message_received: after
                     // AESS_keywrap_cmd_0x02
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "AESS_message_received: after AESS_keywrap_cmd_0x02:"
                " status: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2);
        break;
    case 0x82140324: // SEP_Driver__Mailbox_Rx
        memcpy((void *)&m + 0x00, &s->debug_trace_regs[offset + 0x88],
               sizeof(uint32_t));
        memcpy((void *)&m + 0x04, &s->debug_trace_regs[offset + 0x90],
               sizeof(uint32_t));
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Mailbox_Rx:"
                " endpoint: 0x%02x tag: 0x%02x opcode: "
                "0x%02x(%u) param: 0x%02x data: 0x%02x\n",
                tid, tid_str, m.ep, m.tag, m.op, m.op, m.param, m.data);
        break;
    case 0x82140328: // SEP_Driver__Mailbox_RxMessageQueue
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Mailbox_RxMessageQueue:"
                " endpoint: 0x%02" PRIX64 " opcode: 0x%02" PRIX64 " arg4: "
                "0x%02" PRIX64 " arg5: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82140334: // SEP_Driver__Mailbox_ReadMsgFetch
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Mailbox_ReadMsgFetch:"
                " endpoint: 0x%02" PRIX64 " data: 0x%02" PRIX64
                " data2: 0x%02" PRIX64 " "
                "read_msg.data[0]: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82140338: // SEP_Driver__Mailbox_ReadBlocked
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Mailbox_ReadBlocked:"
                " for_TRNG_ASC0_ASC1_read_0 returned False: "
                "data0: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2);
        break;
    case 0x8214033C: // SEP_Driver__Mailbox_ReadComplete
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Mailbox_ReadComplete:"
                " for_TRNG_ASC0_ASC1_read_0 returned True: "
                "data0: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2);
        break;
    case 0x82140340: // SEP_Driver__Mailbox_Tx
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Mailbox_Tx:"
                " function_13 returned True:  arg2: 0x%02" PRIX64 " "
                "arg3: 0x%02" PRIX64 " arg4: 0x%02" PRIX64 " arg5: 0x%02" PRIX64
                "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82140344: // SEP_Driver__Mailbox_TxStall
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Mailbox_TxStall:"
                " function_13 returned False: arg2: 0x%02" PRIX64 " "
                "arg3: 0x%02" PRIX64 " arg4: 0x%02" PRIX64 " arg5: 0x%02" PRIX64
                "\n",
                tid, tid_str, arg2, arg3, arg4, arg5);
        break;
    case 0x82140348: // mod_ASC0_ASC1_function_message_received:
                     // method_0x4131/Mailbox_OOL_In
    case 0x8214034C: // mod_ASC0_ASC1_function_message_received:
                     // Mailbox_OOL_Out
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: SEP "
                "mod_ASC0_ASC1_function_message_received "
                "SEP_Driver: Mailbox_OOL_%s:"
                " arg2: 0x%02" PRIX64 " arg3: 0x%02" PRIX64
                " arg4: 0x%02" PRIX64 "\n",
                tid, tid_str, (trace_id == 0x82140348) ? "In" : "Out", arg2,
                arg3, arg4);
        break;
    case 0x82140360: // SEP_Driver__Mailbox_Wake
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Mailbox_Wake:"
                " current value: registers[0x4108]: 0x%08" PRIX64 " "
                "SEP_message_incoming: %" PRIu64 "\n",
                tid, tid_str, arg2, arg3);
        break;
    case 0x82140364: // SEP_Driver__Mailbox_NoData
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "SEP_Driver__Mailbox_NoData:"
                " current value: registers[0x4108]: 0x%08" PRIX64 "\n",
                tid, tid_str, arg2);
        break;
    case 0x82140964: // PMGR_message_received
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "PMGR_message_received:"
                " fromto: 0x%02" PRIX64 " data0: 0x%02" PRIX64
                " data1: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2, arg3, arg4);
        break;
    case 0x82140968: // PMGR_enable_clock
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "PMGR_enable_clock:"
                " enable_clock: 0x%02" PRIX64 "\n",
                tid, tid_str, arg2);
        break;
    default: // Unknown trace value
        DPRINTF("DEBUG_TRACE: Description: tid: 0x%05" PRIX64 "/%s: "
                "Unknown trace_id 0x%02" PRIX64 ":"
                " arg2: 0x%02" PRIX64 " arg3: 0x%02" PRIX64
                " arg4: 0x%02" PRIX64 " "
                "arg5: 0x%02" PRIX64 "\n",
                tid, tid_str, trace_id, arg2, arg3, arg4, arg5);
        break;
    }
}

static uint64_t debug_trace_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    if (!s->shmbuf_base) {
        qemu_log_mask(
            LOG_UNIMP,
            "DEBUG_TRACE: SHMBUF_BASE==NULL: Unknown read at 0x" HWADDR_FMT_plx
            " size=%u\n",
            addr, size);
        return 0;
    }
    switch (addr) {
    case 0x0:
        return 0xFFFFFFFF; // negated trace exclusion mask for wrapper
    case 0x4: // some index
    case 0x18: // unknown0
    case 0x40: // unknown1
        goto jump_default;
    case 0x1C:
        return 0x0; // disable trace mask for inner function
    case 0x20:
        return 0xFFFFFFFF; // trace mask for inner function
    default:
        qemu_log_mask(LOG_UNIMP,
                      "DEBUG_TRACE: Unknown read at 0x" HWADDR_FMT_plx
                      " size=%u ret==0x%" PRIX64 "\n",
                      addr, size, ret);
    jump_default:
        memcpy(&ret, &s->debug_trace_regs[addr], size);
        break;
    }
    return ret;
}

static const MemoryRegionOps debug_trace_reg_ops = {
    .write = debug_trace_reg_write,
    .read = debug_trace_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .valid.unaligned = false,
};


#define REG_TRNG_FIFO_OUTPUT_BASE (0x00)
#define REG_TRNG_FIFO_OUTPUT_END (0x0C)
#define REG_TRNG_STATUS (0x10)
#define TRNG_STATUS_READY BIT(0)
#define TRNG_STATUS_SHUTDOWN_OVFL BIT(1)
#define TRNG_STATUS_STUCK BIT(2)
#define TRNG_STATUS_NOISE_FAIL BIT(3)
#define TRNG_STATUS_RUN_FAIL BIT(4)
#define TRNG_STATUS_LONG_RUN_FAIL BIT(5)
#define TRNG_STATUS_POKER_FAIL BIT(6)
#define TRNG_STATUS_MONOBIT_FAIL BIT(7)
#define TRNG_STATUS_TEST_READY BIT(8)
#define TRNG_STATUS_STUCK_NRBG BIT(9)
#define TRNG_STATUS_REPCNT_FAIL BIT(13)
#define TRNG_STATUS_APROP_FAIL BIT(13)
#define TRNG_STATUS_TEST_STUCK BIT(13)
#define TRNG_STATUS_NEED_CLOCK BIT(31)
#define REG_TRNG_CONTROL (0x14)
#define TRNG_CONTROL_STUCK_NRBG_MASK BIT(10)
#define TRNG_CONTROL_ENABLED BIT(10)
#define TRNG_CONTROL_DRBG_ENABLED BIT(12)
#define TRNG_CONTROL_REP_CNT_FAIL_MASK BIT(13)
#define TRNG_CONTROL_APROP_FAIL_MASK BIT(14)
#define TRNG_CONTROL_RESEED BIT(15)
#define TRNG_CONTROL_REQ_DATA BIT(16)
#define TRNG_CONTROL_REQ_HOLD BIT(17)
#define TRNG_CONTROL_DATA_BLOCKS(v) (((v) >> 20) & 0xFFF)
#define REG_TRNG_CONFIG (0x18)
#define TRNG_CONFIG_NOISE_BLOCKS(v) ((v) & 0xFF)
#define TRNG_CONFIG_SAMPLE_DIV(v) (((v) >> 8) & 0xF)
#define TRNG_CONFIG_READ_TIMEOUT(v) (((v) >> 12) & 0xF)
#define TRNG_CONFIG_SAMPLE_CYCLES(v) (((v) >> 16) & 0xFFFF)
#define REG_TRNG_AES_KEY_BASE (0x40)
#define REG_TRNG_AES_KEY_END (0x5C)
#define REG_TRNG_ECID_LOW (0x60)
#define REG_TRNG_ECID_HI (0x64)
#define REG_TRNG_COUNTER_LOW (0x68)
#define REG_TRNG_COUNTER_HI (0x6C)

static void trng_regs_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned size)
{
    AppleTRNGState *s = opaque;
    AppleSEPState *sep = s->sep;
    uint32_t enabled;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(sep->cpu), stderr, CPU_DUMP_CODE);
#endif

    DPRINTF("TRNG_REGS: Write at 0x" HWADDR_FMT_plx " of value 0x%" PRIX64 "\n",
            addr, data);

    switch (addr) {
    case REG_TRNG_FIFO_OUTPUT_BASE ... REG_TRNG_FIFO_OUTPUT_END:
        if ((s->offset_0x70 & 0x40) != 0) {
            data = bswap32(data);
        }
        memcpy(s->fifo + (addr - REG_TRNG_FIFO_OUTPUT_BASE), &data, size);
        if (addr == REG_TRNG_FIFO_OUTPUT_END &&
            ((s->offset_0x70 & 0x40) != 0)) {
            QCryptoCipher *cipher;

            cipher = qcrypto_cipher_new(QCRYPTO_CIPHER_ALGO_AES_256,
                                        QCRYPTO_CIPHER_MODE_ECB, s->key,
                                        sizeof(s->key), &error_abort);
            g_assert_nonnull(cipher);
            qcrypto_cipher_encrypt(cipher, s->fifo, s->fifo, sizeof(s->fifo),
                                   &error_abort);
            qcrypto_cipher_free(cipher);
        }
        break;
    case REG_TRNG_STATUS:
        enabled = (s->config & TRNG_CONTROL_ENABLED) != 0;
        if ((data & TRNG_STATUS_READY) != 0 && (s->offset_0x70 & 0xC0) == 0) {
            qemu_guest_getrandom_nofail(s->fifo, sizeof(s->fifo));
        }
        break;
    case REG_TRNG_CONTROL: {
        uint32_t old_enabled = (s->config & TRNG_CONTROL_ENABLED) != 0;
        s->config = (uint32_t)data;
        DPRINTF("TRNG_REGS: REG_TRNG_CONTROL write at 0x" HWADDR_FMT_plx
                " of value 0x%" PRIX64 "\n",
                addr, data);
        enabled = (data & TRNG_CONTROL_ENABLED) != 0;

        if (!old_enabled && enabled) {
            apple_a7iop_interrupt_status_push(APPLE_A7IOP(sep)->iop_mailbox,
                                              0x10003); // TRNG
        }
        break;
    }
    case REG_TRNG_AES_KEY_BASE ... REG_TRNG_AES_KEY_END:
        if ((s->offset_0x70 & 0xC0) != 0) {
            data = bswap32(data);
        }
        memcpy(s->key + (addr - REG_TRNG_AES_KEY_BASE), &data, size);
        break;
    case REG_TRNG_ECID_LOW:
        if ((s->offset_0x70 & 0x80) != 0) {
            data = bswap32(data);
        }
        s->ecid &= 0xFFFFFFFF00000000;
        s->ecid |= data & 0xFFFFFFFF;
        break;
    case REG_TRNG_ECID_HI:
        if ((s->offset_0x70 & 0x80) != 0) {
            data = bswap32(data);
        }
        s->ecid &= 0x00000000FFFFFFFF;
        s->ecid |= (data & 0xFFFFFFFF) << 32;
        break;
    case REG_TRNG_COUNTER_LOW:
        if ((s->offset_0x70 & 0x80) != 0) {
            data = bswap32(data);
        }
        s->counter &= 0xFFFFFFFF00000000;
        s->counter |= data & 0xFFFFFFFF;
        break;
    case REG_TRNG_COUNTER_HI:
        if ((s->offset_0x70 & 0x80) != 0) {
            data = bswap32(data);
        }
        s->counter &= 0x00000000FFFFFFFF;
        s->counter |= (data & 0xFFFFFFFF) << 32;
        if ((s->offset_0x70 & 0x80) != 0) {
            uint8_t seed_material[DRBG_CTR_AES256_SEED_SIZE] = { 0 };
            memcpy(seed_material + 0x0, s->key, 0x20);
            memcpy(seed_material + 0x20, &s->ecid, 0x8);
            memcpy(seed_material + 0x28, &s->counter, 0x8);
            if (s->ctr_drbg_init) {
                s->ctr_drbg_init = 0;
                drbg_ctr_aes256_init(&s->ctr_drbg_rng, seed_material);
                memset(s->fifo, 0, sizeof(s->fifo));
            } else {
                drbg_ctr_aes256_update(&s->ctr_drbg_rng.key, &s->ctr_drbg_rng.V,
                                       seed_material);
                drbg_ctr_aes256_random(&s->ctr_drbg_rng, 0x10, s->fifo);
            }
        }
        break;
    case 0x70:
        s->offset_0x70 = data;
        if ((s->offset_0x70 & 0x80) != 0) {
            s->ctr_drbg_init = 1;
        } else if ((s->offset_0x70 & 0x40) == 0) {
            memset(s->key, 0, sizeof(s->key));
        }
        // don't do the encryption here
        break;
    default:
        DPRINTF("TRNG_REGS: Unknown write at 0x" HWADDR_FMT_plx
                " of value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t trng_regs_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleTRNGState *s = opaque;
    AppleSEPState *sep = s->sep;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(sep->cpu), stderr, CPU_DUMP_CODE);
#endif

    uint32_t enabled = (s->config & TRNG_CONTROL_ENABLED) != 0;
    switch (addr) {
    case REG_TRNG_FIFO_OUTPUT_BASE ... REG_TRNG_FIFO_OUTPUT_END:
        memcpy(&ret, s->fifo + (addr - REG_TRNG_FIFO_OUTPUT_BASE), size);
        if ((s->offset_0x70 & 0xC0) != 0) {
            ret = bswap32(ret);
        }
        break;
        break;
    case REG_TRNG_STATUS:
        ret = TRNG_STATUS_READY | TRNG_STATUS_TEST_READY;
        break;
    case REG_TRNG_CONTROL:
        ret = s->config;
        if (enabled) {
            apple_a7iop_interrupt_status_push(APPLE_A7IOP(sep)->iop_mailbox,
                                              0x10003); // TRNG
        }
        break;
    case 0x78: // (value & 0x180000) == 0 == panic
        ret = 0x180000;
        break;
    case REG_TRNG_AES_KEY_BASE ... REG_TRNG_AES_KEY_END:
        memcpy(&ret, s->key + (addr - REG_TRNG_AES_KEY_BASE), size);
        break;
    case REG_TRNG_ECID_LOW:
        ret = s->ecid & 0xFFFFFFFF;
        break;
    case REG_TRNG_ECID_HI:
        ret = (s->ecid & 0xFFFFFFFF00000000) >> 32;
        break;
    case REG_TRNG_COUNTER_LOW:
        ret = s->counter & 0xFFFFFFFF;
        break;
    case REG_TRNG_COUNTER_HI:
        ret = (s->counter & 0xFFFFFFFF00000000) >> 32;
        break;
    case 0x70:
        ret = s->offset_0x70;
        break;
    default:
        DPRINTF("TRNG_REGS: Unknown read at 0x" HWADDR_FMT_plx "\n", addr);
        break;
    }
    DPRINTF("TRNG_REGS: Read at 0x" HWADDR_FMT_plx " ret: 0x%" PRIX64 "\n",
            addr, ret);
    return ret;
}

static const MemoryRegionOps trng_regs_reg_ops = {
    .write = trng_regs_reg_write,
    .read = trng_regs_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};


////

const char *sepos_powerstate_name(uint64_t powerstate_offset);

const char *sepos_powerstate_name(uint64_t powerstate_offset)
{
    switch (powerstate_offset) {
    case 0x10:
        return "AES_HDCP";
    case 0x20: // mod_PKA ; PKA0 ; arg8 is 0xC8
        return "PKA0";
    case 0x28:
        return "TRNG";
    case 0x30: // PKA1
        return "PKA1";
    case 0x48:
        return "I2C";
    case 0x58:
        return "KEY";
    case 0x60:
        return "EISP";
    case 0x68:
        return "SEPD";
    default:
        break;
    }
    return "Unknown";
}


static void pmgr_base_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x10: // mod_AES_HDCP
    case 0x20: // mod_PKA ; PKA0 ; arg8 is 0xC8
    case 0x28: // mod_TRNG
    case 0x30: // PKA1
    case 0x48: // mod_I2C
    case 0x58: // mod_KEY
    case 0x60: // mod_EISP
    case 0x68: // mod_SEPD
        DPRINTF("SEP PMGR_BASE: PowerState %s write before at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                sepos_powerstate_name(addr), addr, data);
        /*
            LIKE AP PMGR
            data | 0x80000000 == RESET
            data | 0x.f == ENABLE
            data | 0x.4 == POWER_SAVE
            data | 0xF. == ENABLED
            data | 0x4. == POWER_SAVE_ACTIVATED?
        */
        data = ((data & 0xF) << 4) | (data & 0xF);
        if ((data & 0xF) == 0xF) {
            if (addr == 0x58) {
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  0x10000); // KEY
            }
            if (addr == 0x48) {
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  0x10002); // I2C
            }
            if (addr == 0x28) {
                // apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                // 0x10003); // TRNG
            }
            if (addr == 0x68) {
                //////apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                /// 0x10003); // TRNG
            }
            // apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
            // 0x10005); // AES_SEP
            // apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
            // 0x10007); // GPIO
            if (addr == 0x20) {
                ////apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                /// 0x1000A); // PKA
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  0x1000B); // PKA
            }
            if (addr == 0x30) {
                //////apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                /// 0x1000A); // PKA2???
            }
            if (addr == 0x10) {
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  0x10009); // AES_HDCP
            }
        }
        // if ((data & 0xF) == 0x4) {
        //     if (addr == 0x48) {
        //         apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
        //                                           0x10002); // I2C
        //     }
        // }

        DPRINTF("SEP PMGR_BASE: PowerState %s write after at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                sepos_powerstate_name(addr), addr, data);
        goto jump_default;
    case 0x8000:
        // the resulting values should only reset on SoC reset
        if ((data & 1) != 0) {
            s->pmgr_fuse_changer_bit0_was_set = true;
        }
        if ((data & 2) != 0) {
            s->pmgr_fuse_changer_bit1_was_set = true;
        }
        DPRINTF("SEP PMGR_BASE: fuse change write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        goto jump_default;
    default:
        DPRINTF("SEP PMGR_BASE: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
    jump_default:
        memcpy(&s->pmgr_base_regs[addr], &data, size);
        break;
    }
}

static uint64_t pmgr_base_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    memcpy(&ret, &s->pmgr_base_regs[addr], size);
    switch (addr) {
    case 0x20: // mod_PKA ; PKA0 ; arg8 is 0xC8
    case 0x28: // mod_TRNG
    case 0x30: // PKA1
    case 0x48: // mod_I2C
    case 0x58: // mod_KEY
    case 0x60: // mod_EISP
    case 0x68: // mod_SEPD
        DPRINTF("SEP PMGR_BASE: PowerState %s read at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                sepos_powerstate_name(addr), addr, ret);
        break;
    case 0x8200:
#ifdef SEP_ENABLE_TRACE_BUFFER
        enable_trace_buffer(s); // for T8015
#endif
        goto jump_default;
    default:
    jump_default:
        DPRINTF("SEP PMGR_BASE: Unknown read at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, ret);
        break;
    }

    return ret;
}

static const MemoryRegionOps pmgr_base_reg_ops = {
    .write = pmgr_base_reg_write,
    .read = pmgr_base_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};


static void key_base_reg_write(void *opaque, hwaddr addr, uint64_t data,
                               unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case SEP_KEY_BASE_LOAD_KEY_OFFSET: // load_key
        DPRINTF("SEP KEY_BASE: Offset 0x" HWADDR_FMT_plx
                ": Input: load_key 0x%" PRIX64 "\n",
                addr, data);
        data &= ~SEP_KEY_BASE_LOAD_KEY_ACTIVE;
        goto jump_default;
    case 0x8: // command or storage index: 0x20-0x26, 0x30-0x31, 0x04 (without
              // input)
        /*
        cmds:
        0x0/0x1: wrapping key primary/secondary cmd7_0x4
        0x2/0x3: auth key primary/secondary cmd7_0x5
        0x6/0x7: cmd7_0x8
        0x8/0x9: cmd7_0x9
        0xA/0xB: sub key primary/secondary cmd7_0x6
        0xC: cmd7_0xB
        0xD: cmd7_0xC
        0xE/0xF: cmd7_0xA
        0x10..0x16: something about Ks and interfaces cmd7_0x3
        0x18..0x1E: send data2==data_size_qwords of data cmd7_0x2(cmd7_0x7)
        0x3F: first 0x40 bytes of random data cmd7_0x7
        0x40: second 0x40 bytes of random data cmd7_0x7
        */
        DPRINTF("SEP KEY_BASE: Offset 0x" HWADDR_FMT_plx
                ": Execute Command/Storage Index: cmd 0x%" PRIX64 "\n",
                addr, data);
        goto jump_default;
    case 0x308 ... 0x344: // 0x40 bytes of output from TRNG
        DPRINTF("SEP KEY_BASE: Offset 0x" HWADDR_FMT_plx
                ": Input: cmd 0x%" PRIX64 "\n",
                addr, data);
        goto jump_default;
    default:
    jump_default:
        memcpy(&s->key_base_regs[addr], &data, size);
        DPRINTF("SEP KEY_BASE: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t key_base_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case SEP_KEY_BASE_LOAD_KEY_OFFSET:
        DPRINTF("SEP KEY_BASE: LOAD_KEY read-back. read at 0x" HWADDR_FMT_plx
                "\n", addr);
        goto jump_default;
    case 0x40 ... 0x248:
        // actual size 0x20a
        DPRINTF("SEP KEY_BASE: data0 read. read at 0x" HWADDR_FMT_plx "\n",
                addr);
        goto jump_default;
    default:
    jump_default:
        memcpy(&ret, &s->key_base_regs[addr], size);
        DPRINTF("SEP KEY_BASE: Unknown read at 0x" HWADDR_FMT_plx "\n", addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps key_base_reg_ops = {
    .write = key_base_reg_write,
    .read = key_base_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};


static void key_fkey_reg_write(void *opaque, hwaddr addr, uint64_t data,
                               unsigned size)
{
    AppleSEPState *s = opaque;
    AppleA7IOP *a7iop = APPLE_A7IOP(s);

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
    jump_default:
        memcpy(&s->key_fkey_regs[addr], &data, size);
        DPRINTF("SEP KEY_FKEY: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t key_fkey_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;
    uint8_t key_fkey_offset_0x14_index = 0;
    uint8_t key_fkey_offset_0x14_index_limit = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&ret, &s->key_fkey_regs[addr], size);
        DPRINTF("SEP KEY_FKEY: Unknown read at 0x" HWADDR_FMT_plx
                " ret: 0x%" PRIX64 "\n",
                addr, ret);
        break;
    }

    return ret;
}

static const MemoryRegionOps key_fkey_reg_ops = {
    .write = key_fkey_reg_write,
    .read = key_fkey_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};


static void key_fcfg_reg_write(void *opaque, hwaddr addr, uint64_t data,
                               unsigned size)
{
    AppleSEPState *s = opaque;
    AppleA7IOP *a7iop = APPLE_A7IOP(s);

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif

    switch (addr) {
    case 0x0:
        DPRINTF("SEP KEY_FCFG: TEST0 0x" HWADDR_FMT_plx " with value 0x%" PRIX64
                "\n",
                addr, data);

        goto jump_default;
    case 0x4:
        DPRINTF("SEP KEY_FCFG: TEST1 0x" HWADDR_FMT_plx " with value 0x%" PRIX64
                "\n",
                addr, data);
        goto jump_default;
    case 0xc:
        DPRINTF("SEP KEY_FCFG: TEST2 0x" HWADDR_FMT_plx " with value 0x%" PRIX64
                "\n",
                addr, data);
        goto jump_default;
    case 0x10:
        if (data == 0x1) {
            ((uint32_t *)s->key_base_regs)[0x00 / 4] = (1 << 31) | (1 << 0);
        }
        goto jump_default;
    case 0x14:
        DPRINTF("SEP KEY_FCFG: vals 0x" HWADDR_FMT_plx " with value 0x%" PRIX64
                "\n",
                addr, data);
        if (data == 0xFFFF) {
            s->key_fcfg_offset_0x14_index = 0x0;
            memset(s->key_fcfg_offset_0x14_values, 0,
                   sizeof(s->key_fcfg_offset_0x14_values));
        }
        uint8_t index = s->key_fcfg_offset_0x14_index;
        uint8_t index_limit = sizeof(s->key_fcfg_offset_0x14_values) /
                              sizeof(s->key_fcfg_offset_0x14_values[0]);
        index = (index < index_limit) ? index : 0;
        s->key_fcfg_offset_0x14_values[index] = data & 0xFFFF;
        s->key_fcfg_offset_0x14_index++;
        goto jump_default;
    default:
    jump_default:
        memcpy(&s->key_fcfg_regs[addr], &data, size);
        DPRINTF("SEP KEY_FCFG: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t key_fcfg_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;
    uint8_t key_fcfg_offset_0x14_index;
    uint8_t key_fcfg_offset_0x14_index_limit;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x14:
        // it'll complain otherwise:
        // ../hw/arm/apple-silicon/sep.c:1541:9: warning: label followed by a
        // declaration is a C23 extension [-Wc23-extensions]
        key_fcfg_offset_0x14_index = s->key_fcfg_offset_0x14_index;
        key_fcfg_offset_0x14_index_limit =
            sizeof(s->key_fcfg_offset_0x14_values) /
            sizeof(s->key_fcfg_offset_0x14_values[0]);
        key_fcfg_offset_0x14_index =
            (key_fcfg_offset_0x14_index < key_fcfg_offset_0x14_index_limit) ?
                key_fcfg_offset_0x14_index :
                0;
        ret = ((uint32_t)key_fcfg_offset_0x14_index << 16) |
              s->key_fcfg_offset_0x14_values[key_fcfg_offset_0x14_index];
        DPRINTF("SEP KEY_FCFG: vals read at 0x" HWADDR_FMT_plx
                " ret: 0x%" PRIX64 "\n",
                addr, ret);
        break;
    case 0x18: // for SKG ; 0x4 | (value & 0x3)
        // ret = 0x4 | 0x0; // when AMK is disabled
        ret = 0x4 | 0x1; // when AMK is enabled
        DPRINTF("SEP KEY_FCFG: AMK read at 0x" HWADDR_FMT_plx " ret: 0x%" PRIX64
                "\n",
                addr, ret);
        break;
    default:
        memcpy(&ret, &s->key_fcfg_regs[addr], size);
        DPRINTF("SEP KEY_FCFG: Unknown read at 0x" HWADDR_FMT_plx
                " ret: 0x%" PRIX64 "\n",
                addr, ret);
        break;
    }

    return ret;
}

static const MemoryRegionOps key_fcfg_reg_ops = {
    .write = key_fcfg_reg_write,
    .read = key_fcfg_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};


static void moni_base_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&s->moni_base_regs[addr], &data, size);
        DPRINTF("SEP MONI_BASE: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t moni_base_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&ret, &s->moni_base_regs[addr], size);
        DPRINTF("SEP MONI_BASE: Unknown read at 0x" HWADDR_FMT_plx "\n", addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps moni_base_reg_ops = {
    .write = moni_base_reg_write,
    .read = moni_base_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};


static void moni_thrm_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&s->moni_thrm_regs[addr], &data, size);
        DPRINTF("SEP MONI_THRM: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t moni_thrm_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&ret, &s->moni_thrm_regs[addr], size);
        DPRINTF("SEP MONI_THRM: Unknown read at 0x" HWADDR_FMT_plx "\n", addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps moni_thrm_reg_ops = {
    .write = moni_thrm_reg_write,
    .read = moni_thrm_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};


static void eisp_base_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&s->eisp_base_regs[addr], &data, size);
        DPRINTF("SEP EISP_BASE: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t eisp_base_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&ret, &s->eisp_base_regs[addr], size);
        DPRINTF("SEP EISP_BASE: Unknown read at 0x" HWADDR_FMT_plx "\n", addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps eisp_base_reg_ops = {
    .write = eisp_base_reg_write,
    .read = eisp_base_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};


static void eisp_hmac_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&s->eisp_hmac_regs[addr], &data, size);
        DPRINTF("SEP EISP_HMAC: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t eisp_hmac_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&ret, &s->eisp_hmac_regs[addr], size);
        DPRINTF("SEP EISP_HMAC: Unknown read at 0x" HWADDR_FMT_plx "\n", addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps eisp_hmac_reg_ops = {
    .write = eisp_hmac_reg_write,
    .read = eisp_hmac_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};

static QCryptoCipherAlgo get_aes_cipher_alg(uint32_t flags)
{
    switch (flags & (SEP_AESS_CMD_FLAG_KEYSIZE_AES128 |
                     SEP_AESS_CMD_FLAG_KEYSIZE_AES192 |
                     SEP_AESS_CMD_FLAG_KEYSIZE_AES256)) {
    case SEP_AESS_CMD_FLAG_KEYSIZE_AES128:
        return QCRYPTO_CIPHER_ALGO_AES_128;
    case SEP_AESS_CMD_FLAG_KEYSIZE_AES192:
        return QCRYPTO_CIPHER_ALGO_AES_192;
    case SEP_AESS_CMD_FLAG_KEYSIZE_AES256:
        return QCRYPTO_CIPHER_ALGO_AES_256;
    default:
        g_assert_not_reached();
    }
}

static void xor_32bit_value(uint8_t *dest, uint32_t val, int size)
{ // size in dwords
    // TODO: ASAN complains about uint32_t*, wants uint16_t* or even uint8_t* ;;
    // was most likely about two single bool's between array, probably fixed.
    uint32_t *ptr = (uint32_t *)dest;
    for (int i = 0; i < size; i++) {
        *ptr++ ^= val;
    }
}

static void aess_raise_interrupt(AppleAESSState *s)
{
    AppleSEPState *sep = s->sep;
    s->interrupt_status |= SEP_AESS_REGISTER_INTERRUPT_STATUS_DONE;
    if ((s->interrupt_enabled & SEP_AESS_REGISTER_INTERRUPT_ENABLED_MASK) ==
        (SEP_AESS_REGISTER_INTERRUPT_ENABLED_INTERRUPT_ENABLED |
         SEP_AESS_REGISTER_INTERRUPT_ENABLED_RAISE_ON_COMPLETION)) {
        apple_a7iop_interrupt_status_push(APPLE_A7IOP(sep)->iop_mailbox,
                                          0x10005); // AESS
    }
}

// TODO: This is 100% wrong, but it works anyhow/anyway.
// Somewhen, I'll have to handle keyunwrap (if that exists) and PKA.
// For the PKA ECDH command, reuse code from SSC.

static void aess_keywrap_uid(AppleAESSState *s, uint8_t *in, uint8_t *out,
                             QCryptoCipherAlgo cipher_alg, uint32_t cmd,
                             uint32_t reg_0x18_keydisable)
{ // for keywrap only
    // TODO: Second half of output might be CMAC!!!
    g_assert_cmpuint(cipher_alg, ==, QCRYPTO_CIPHER_ALGO_AES_256);
    QCryptoCipher *cipher;
    uint32_t normalized_cmd = SEP_AESS_CMD_WITHOUT_FLAGS(cmd);
    size_t key_len = qcrypto_cipher_get_key_len(cipher_alg);
    size_t data_len = 0x20;
    g_assert_cmpuint(data_len, ==, 0x20);
    uint8_t used_key[0x20] = { 0 };
    if (normalized_cmd == 0x02 && s->keywrap_uid0_enabled) {
        memcpy(used_key, (uint8_t *)s->keywrap_key_uid0,
               sizeof(used_key)); // for UUID
    } else if (normalized_cmd == 0x12 && s->keywrap_uid1_enabled) {
        memcpy(used_key, (uint8_t *)s->keywrap_key_uid1,
               sizeof(used_key)); // for UUID
    } else if (normalized_cmd == 0x02 || normalized_cmd == 0x12) {
        memcpy(used_key, (uint8_t *)AESS_UID_SEED_NOT_ENABLED,
               sizeof(used_key));
    } else {
        g_assert_not_reached();
    }
    // TODO: Dirty hack, so iteration_register being set/unset shouldn't result
    // in the same output keys.
    xor_32bit_value(&used_key[0x10], s->reg_0x14_keywrap_iterations_counter,
                    0x8 / 4); // seed_bits are only for keywrap
    DPRINTF("%s: cmd: 0x%02x normalized_cmd: 0x%02x cipher_alg: %u; "
            "key_len: %lu; iterations: %u, seed_bits: 0x%02x, "
            "reg_0x18_keydisable: 0x%02x\n",
            __func__, cmd, normalized_cmd, cipher_alg, key_len,
            s->reg_0x14_keywrap_iterations_counter, s->seed_bits,
            reg_0x18_keydisable);
    HEXDUMP("aess_keywrap_uid: used_key", used_key, sizeof(used_key));
    HEXDUMP("aess_keywrap_uid: in", in, data_len);
    cipher = qcrypto_cipher_new(cipher_alg, QCRYPTO_CIPHER_MODE_CBC, used_key,
                                key_len, &error_abort);
    g_assert_nonnull(cipher);
    uint8_t iv[0x10] = { 0 };
    qcrypto_cipher_setiv(cipher, iv, sizeof(iv), &error_abort);
    uint8_t enc_temp[0x20] = { 0 };
    memcpy(enc_temp, in, sizeof(enc_temp));

    // TODO: iteration register is actually for the iterations inside the
    // algorithm, not how often the algorihm is being called.
    if (s->reg_0x14_keywrap_iterations_counter == 0) {
        s->reg_0x14_keywrap_iterations_counter = 1;
    }
    while (s->reg_0x14_keywrap_iterations_counter) {
        qcrypto_cipher_encrypt(cipher, enc_temp, enc_temp, sizeof(enc_temp),
                               &error_abort);
        s->reg_0x14_keywrap_iterations_counter--;
    }

    memcpy(out, enc_temp, data_len);
    HEXDUMP("aess_keywrap_uid: out1", out, data_len);
    s->reg_0x14_keywrap_iterations_counter = 0;
    qcrypto_cipher_free(cipher);
    // interrupts are normally only raised by driver_ops 0x4/0x1D (keywrap) if
    // iterations_counter is over 10/0xA, but don't take that for granted.
    // aess_raise_interrupt(s); // run it always instead
}

static int aess_get_custom_keywrap_index(uint32_t cmd)
{
    switch (cmd) {
    case 0x01:
    case 0x06:
        return 0;
    case 0x41:
    case 0x46:
        return 1;
    case 0x81:
    case 0x08:
    case 0x88:
        return 2;
    case 0xC1:
    case 0x48:
    case 0xC8:
        return 3;
    default:
        g_assert_not_reached();
    }
}

static bool
check_register_0x18_KEYDISABLE_BIT_INVALID(uint32_t cmd,
                                           uint32_t reg_0x18_keydisable)
{
    uint32_t cmd_without_keysize = SEP_AESS_CMD_WITHOUT_KEYSIZE(cmd);
    bool reg_0x18_keydisable_bit0 = (reg_0x18_keydisable & 0x1) != 0;
    bool reg_0x18_keydisable_bit1 = (reg_0x18_keydisable & 0x2) != 0;
    bool reg_0x18_keydisable_bit3 = (reg_0x18_keydisable & 0x8) != 0;
    bool reg_0x18_keydisable_bit4 = (reg_0x18_keydisable & 0x10) != 0;
    switch (cmd_without_keysize) {
    // case 0x: // driver_op == 0x09 (cmd 0x00, invalid)
    case 0x0C:
    case 0x4C:
        // cmd 0x0C or 0x4C might be driver_op 0x09, if it would exist.
        return reg_0x18_keydisable_bit4;
    // driver_op == 0x0A/0x0D (cmds 0x00/0x00, both are invalid)
    case 0x09: // driver_op 0x0A would be most likely cmd 0x09, if using it in
               // the _operate function would be allowed
    case 0x0A: // driver_op 0x0D would be most likely cmd 0x0A, if using it in
               // the _operate function would be allowed
        return reg_0x18_keydisable_bit0;
    // driver_op == 0x0B/0x0E (cmds 0x49/0x00, 0x0E is invalid)
    case 0x49:
    case 0x4A: // driver_op 0x0E would be most likely cmd 0x4A, if using it in
               // the _operate function would be allowed
        return reg_0x18_keydisable_bit1;
    // driver_op == 0x13/0x14 (cmds 0x0D/0x00, 0x14 is invalid)
    case 0x0D: // 0x0D and 0x4D, are those actually implemented in real
               // hardware?
    case 0x4D: // driver_op 0x14 would be most likely cmd 0x4D, if using it in
               // the _operate function would be allowed
        return reg_0x18_keydisable_bit3;
    // driver_op == 0x23/0x24 (cmds 0x50/0x90)
    case 0x50:
    case 0x90:
        // driver_ops 0x23/0x24 are not available on iOS 12, but they're on iOS
        // 14
        return reg_0x18_keydisable_bit3;
    default:
        break;
    }
    return false;
}

static void aess_handle_cmd(AppleAESSState *s)
{
    uint32_t cmd = s->command;
    uint32_t reg_0x18_keydisable = s->reg_0x18_keydisable;

    bool keyselect_non_gid0 = SEP_AESS_CMD_FLAG_KEYSELECT_GID1_CUSTOM(cmd) != 0;
    bool keyselect_gid1 = (cmd & SEP_AESS_CMD_FLAG_KEYSELECT_GID1) != 0;
    bool keyselect_custom = (cmd & SEP_AESS_CMD_FLAG_KEYSELECT_CUSTOM) != 0;
    uint32_t normalized_cmd = SEP_AESS_CMD_WITHOUT_FLAGS(cmd);
    QCryptoCipherAlgo cipher_alg = get_aes_cipher_alg(cmd);
    size_t key_len = qcrypto_cipher_get_key_len(cipher_alg);
    bool zero_iv_two_blocks_encryption = false;
    bool register_0x18_KEYDISABLE_BIT_INVALID =
        check_register_0x18_KEYDISABLE_BIT_INVALID(cmd, reg_0x18_keydisable);
    bool valid_command = true;
    bool invalid_parameters = register_0x18_KEYDISABLE_BIT_INVALID;
#if 1
    // not correct behavior, but SEPFW likes to complain if it doesn't expect
    // the output to be zero, so keep it.
    memset(s->out_full, 0, sizeof(s->out_full));
#endif
#if 1
    // not GID1 && not Custom ; ignore the keysize flags here
    if (!keyselect_non_gid0 && normalized_cmd == SEP_AESS_COMMAND_0xB) {
        {
            memset(s->key_256_in, 0, sizeof(s->key_256_in));
            memcpy(s->key_256_in, s->in_full, sizeof(s->in_full));
        }
    }
#endif
    // Not GID1 && not Custom ; Always AES256!!
    else if (!keyselect_non_gid0 &&
             (normalized_cmd == 0x2 || normalized_cmd == 0x12)) {
#if 1
        cipher_alg = QCRYPTO_CIPHER_ALGO_AES_256;
        // VERY important, otherwise key_len would be too short in case that
        // flag 0x200 is missing.
        key_len = qcrypto_cipher_get_key_len(cipher_alg);
        // keyselect_gid1 (= true) variable has no use here
        // key wrapping/deriving data
        uint8_t key_wrap_data_in[0x20] = { 0 };
        uint8_t key_wrap_data_out[0x20] = { 0 };
        memcpy(key_wrap_data_in, s->in_full, key_len);
        // aess_encrypt_decrypt_uid(s, key_wrap_data_in, key_wrap_data_out,
        // cipher_alg, true);
        ////aess_keywrap_uid(s, key_wrap_data_in, key_wrap_data_out, cipher_alg,
        /// false);
        // aess_keywrap_uid(s, key_wrap_data_in, key_wrap_data_out, cipher_alg,
        // true);
        aess_keywrap_uid(s, key_wrap_data_in, key_wrap_data_out, cipher_alg,
                         cmd, reg_0x18_keydisable);
        // qemu_guest_getrandom_nofail(key_wrap_data_out, sizeof(
        // key_wrap_data_out)); // For testing if random output breaks stuff.
        memcpy(s->out_full, key_wrap_data_out, key_len);
#endif
    }
#if 1
    else if (
        normalized_cmd == SEP_AESS_COMMAND_ENCRYPT_CBC ||
        normalized_cmd == SEP_AESS_COMMAND_DECRYPT_CBC ||
        normalized_cmd == SEP_AESS_COMMAND_ENCRYPT_CBC_FORCE_CUSTOM_AES256 ||
        normalized_cmd ==
            SEP_AESS_COMMAND_ENCRYPT_CBC_ONLY_NONCUSTOM_FORCE_CUSTOM_AES256) /* GID0 || GID1 || Custom */
    {
        bool custom_encryption = false;
        DPRINTF("%s: cmd 0x%03x ; ", __func__, cmd);
        HEXDUMP("s->in_full", s->in_full, sizeof(s->in_full));
        if (normalized_cmd ==
            SEP_AESS_COMMAND_ENCRYPT_CBC_ONLY_NONCUSTOM_FORCE_CUSTOM_AES256) {
            if (keyselect_custom) { // 0x80
                goto jump_return; // valid: 0x206, 0x246; invalid: 0x286, 0x2C6
            }
            normalized_cmd = SEP_AESS_COMMAND_ENCRYPT_CBC_FORCE_CUSTOM_AES256;
        }
        if (normalized_cmd ==
            SEP_AESS_COMMAND_ENCRYPT_CBC_FORCE_CUSTOM_AES256) {
            if (!keyselect_custom) {
                zero_iv_two_blocks_encryption = true;
            }
            custom_encryption = true;
            // use_aes256 = true; // variable only used for gid decryption
            keyselect_non_gid0 = true;
            keyselect_gid1 = false;
            keyselect_custom = true;
            normalized_cmd = SEP_AESS_COMMAND_ENCRYPT_CBC;
            cipher_alg = QCRYPTO_CIPHER_ALGO_AES_256;
            key_len = qcrypto_cipher_get_key_len(cipher_alg);
        }
        bool do_encryption = (normalized_cmd == SEP_AESS_COMMAND_ENCRYPT_CBC);
        uint8_t used_key[0x20] = { 0 };
        if (custom_encryption) {
            int custom_keywrap_index =
                aess_get_custom_keywrap_index(cmd & 0xFF);
            if (s->custom_key_index_enabled[custom_keywrap_index]) {
                memcpy(used_key, s->custom_key_index[custom_keywrap_index],
                       sizeof(used_key));
            }
            // Custom takes precedence over GID0 or GID1
        } else if (keyselect_custom) {
            memcpy(used_key, s->key_256_in, sizeof(used_key)); // for custom
        } else {
            if (register_0x18_KEYDISABLE_BIT_INVALID) {
                memcpy(used_key, (uint8_t *)AESS_KEY_FOR_DISABLED_KEY,
                       sizeof(used_key));
            } else if (keyselect_gid1) {
                memcpy(used_key, (uint8_t *)AESS_GID1,
                       sizeof(used_key)); // for GID1
            } else {
                memcpy(used_key, (uint8_t *)AESS_GID0,
                       sizeof(used_key)); // for GID0
            }
        }
        QCryptoCipher *cipher;
        cipher = qcrypto_cipher_new(cipher_alg, QCRYPTO_CIPHER_MODE_CBC,
                                    used_key, key_len, &error_abort);
        g_assert_nonnull(cipher);
        uint8_t iv[0x10] = { 0 };
        uint8_t in[0x10] = { 0 };
        if (do_encryption) {
            memcpy(iv, s->iv, sizeof(iv));
            memcpy(in, s->in, sizeof(in));
            //} else if (normalized_cmd == SEP_AESS_COMMAND_DECRYPT_CBC) {
        } else {
            memcpy(iv, s->iv_dec, sizeof(iv));
            memcpy(in, s->in_dec, sizeof(in));
        }
        if (zero_iv_two_blocks_encryption) {
            memset(iv, 0, sizeof(iv));
            qcrypto_cipher_setiv(
                cipher, iv, sizeof(iv),
                &error_abort); // sizeof(iv) == 0x10 on 256 and 128
            qcrypto_cipher_encrypt(cipher, s->in_full, s->out_full,
                                   sizeof(s->in_full), &error_abort);
            // if ((cmd & 0xF) == 0x9)
        } else if (do_encryption) {
            qcrypto_cipher_setiv(
                cipher, iv, sizeof(iv),
                &error_abort); // sizeof(iv) == 0x10 on 256 and 128
            qcrypto_cipher_encrypt(cipher, s->in, s->out, sizeof(s->in),
                                   &error_abort);
            memcpy(s->tag_out, iv, sizeof(iv));
        } else {
            qcrypto_cipher_decrypt(cipher, in, s->tag_out, sizeof(in),
                                   &error_abort);
            qcrypto_cipher_setiv(
                cipher, iv, sizeof(iv),
                &error_abort); // sizeof(iv) == 0x10 on 256 and 128
            qcrypto_cipher_decrypt(cipher, in, s->out, sizeof(in),
                                   &error_abort);
        }
        qcrypto_cipher_free(cipher);
    }
#endif
#if 1
    // cmd 0x40 == sync seed_bits for keywrap cmd 0x2
    // effect for wrap/UID, no effect for GID/custom?
    else if (normalized_cmd == 0x00) {
        if (keyselect_gid1) {
            memcpy(s->keywrap_key_uid0, (uint8_t *)AESS_UID0,
                   sizeof(s->keywrap_key_uid0)); // for UUID
            xor_32bit_value(&s->keywrap_key_uid0[0x8], s->seed_bits,
                            0x8 / 4); // seed_bits are only for keywrap
            ////xor_32bit_value(&s->keywrap_key_uid0[0x18],
            /// s->reg_0x18_KEYDISABLE, 0x8/4);
            // NOT AFFECTED by REG_0x18???
            s->keywrap_uid0_enabled = true;
            DPRINTF("SEP AESS_BASE: %s: Copied seed_bits for uid0 0x%X\n",
                    __func__, s->seed_bits);
        }
    }
#endif
#if 1
    // cmd 0x50 == sync seed_bits for keywrap cmd 0x12
    else if (normalized_cmd == 0x10) {
        if (keyselect_gid1) {
            // this is conditional memcpy is actually needed, because the result
            // will change if reg_0x18_BIT3 is set
            if (invalid_parameters) {
                memcpy(s->keywrap_key_uid1, (uint8_t *)AESS_UID_SEED_INVALID,
                       sizeof(s->keywrap_key_uid1));
            } else {
                memcpy(s->keywrap_key_uid1, (uint8_t *)AESS_UID1,
                       sizeof(s->keywrap_key_uid1)); // for UUID
            }
            // this xor should happen, even if invalid_parameters is activated
            xor_32bit_value(&s->keywrap_key_uid1[0x8], s->seed_bits,
                            0x8 / 4); // seed_bits are only for keywrap
            ////// NOT AFFECTED by REG_0x18???
            /// xor_32bit_value(&s->keywrap_key_uid1[0x18],
            /// s->reg_0x18_KEYDISABLE, 0x8/4);
            // actually affected by reg_0x18?
            s->keywrap_uid1_enabled = true;
            DPRINTF("SEP AESS_BASE: %s: Copied seed_bits for uid1 0x%X\n",
                    __func__, s->seed_bits);
        }
    }
#endif
#if 0
    // cmd 0xc0 AESSEP_OPERATION_CREATE_D
    // cmd 0x43 AESSEP_OPERATION_CREATE_FROM_GEN2D
#endif
#if 1
    // sync/set key for command 0x206(0x201), 0x246(0x241), 0x208/0x288(0x281),
    // 0x248/0x2C8(0x2C1)
    else if (normalized_cmd == 0x1) {
        int custom_keywrap_index = aess_get_custom_keywrap_index(cmd & 0xFF);
        memcpy(s->custom_key_index[custom_keywrap_index], s->in_full,
               sizeof(s->custom_key_index[custom_keywrap_index]));
        // unset (real zero-key) != zero-key set (not real zero-key)
        xor_32bit_value(s->custom_key_index[custom_keywrap_index], 0xDEADBEEF,
                        0x20 / 4);
        s->custom_key_index_enabled[custom_keywrap_index] = true;
        DPRINTF("SEP AESS_BASE: %s: sync/set key command 0x%02x "
                "cmd 0x%02x\n",
                __func__, normalized_cmd, cmd);
    }
#endif
// TODO: other sync commands: 0x205(0x201), 0x204(0x281), 0x245(0x241),
// 0x244(0x2C1)
#if 0
    else if (normalized_cmd == 0x...)
    {
    }
#endif
    else {
        DPRINTF("SEP AESS_BASE: %s: Unknown command 0x%02x\n", __func__, cmd);
        // valid_command = false;
    }

jump_return:
    // comment this out when not using async
    // if using QEMU_LOCK_GUARD (non-WITH_) in write
    WITH_QEMU_LOCK_GUARD(&s->lock)
    {
        invalid_parameters |= !valid_command;
        if (invalid_parameters) {
            // always keep this flag
            s->interrupt_status |=
                SEP_AESS_REGISTER_INTERRUPT_STATUS_UNRECOVERABLE_ERROR_INTERRUPT;
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: unrecoverable_error just got raised, SEP will "
                          "panic soon.: cmd 0x%03x\n",
                          __func__, cmd);
        }
        s->status &= ~SEP_AESS_REGISTER_STATUS_ACTIVE;
        // call raise_interrupt always instead of only on keywrap, because it's
        // checking conditions
        aess_raise_interrupt(s);
    }
}

static void aess_handle_cmd_bh(void *opaque)
{
    AppleAESSState *s = opaque;
    aess_handle_cmd(s);
}

static void aess_base_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned size)
{
    AppleAESSState *s = opaque;
    AppleSEPState *sep = s->sep;
    uint64_t orig_data = data;

    // QEMU_LOCK_GUARD(&s->lock);

#ifdef ENABLE_CPU_DUMP_STATE
    DPRINTF("\n");
    cpu_dump_state(CPU(sep->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case SEP_AESS_REGISTER_STATUS: // Status
        if ((data & SEP_AESS_REGISTER_STATUS_RUN_COMMAND) != 0) {
            data &= ~SEP_AESS_REGISTER_STATUS_RUN_COMMAND;
            data |= SEP_AESS_REGISTER_STATUS_ACTIVE;
            WITH_QEMU_LOCK_GUARD(&s->lock)
            {
                s->status = data; // surely no bitwise OR?
                s->interrupt_status &= ~SEP_AESS_REGISTER_INTERRUPT_STATUS_DONE;
            }
            aess_handle_cmd(s);
            // qemu_bh_schedule(s->command_bh);
        }
        goto jump_log;
    case SEP_AESS_REGISTER_COMMAND: // Command
        data &= SEP_AESS_CMD_MASK; // for T8020
        s->command = data;
        goto jump_log;
    case SEP_AESS_REGISTER_INTERRUPT_STATUS: // Interrupt Status
        WITH_QEMU_LOCK_GUARD(&s->lock)
        {
            if ((data & SEP_AESS_REGISTER_INTERRUPT_STATUS_DONE) != 0) {
                s->interrupt_status &= ~SEP_AESS_REGISTER_INTERRUPT_STATUS_DONE;
            }
        }
        goto jump_log;
    case SEP_AESS_REGISTER_INTERRUPT_ENABLED: // Interrupt Enabled
        // bit1 == maybe enable interrupt(s)
        // bit0 == maybe activate interrupt when command is done ;
        // ... used for keywrap with > 10/0xA iterations
        data &= SEP_AESS_REGISTER_INTERRUPT_ENABLED_MASK;
        s->interrupt_enabled = data;
        goto jump_log;
    case SEP_AESS_REGISTER_0x14_KEYWRAP_ITERATIONS_COUNTER: // has affect on
                                                            // keywrap
        s->reg_0x14_keywrap_iterations_counter = data;
        goto jump_log;
    case SEP_AESS_REGISTER_0x18_KEYDISABLE: // has affect on keywrap
        data |= s->reg_0x18_keydisable;
        data &= 0x1B;
        s->reg_0x18_keydisable = data;
        goto jump_log;
    case SEP_AESS_REGISTER_SEED_BITS: // seed_bits ;; has affect on keywrap ;;
                                      // offset 0x1C == flags offset: stores
                                      // flags, like if the device has been
                                      // demoted (bit 30). On T8010, the bits
                                      // are between 28 and 31, on T8020, the
                                      // bits are between 27 and 31.
        data &= ~s->seed_bits_lock;
        data |= s->seed_bits & s->seed_bits_lock;
        s->seed_bits = data;
        goto jump_log;
    case SEP_AESS_REGISTER_SEED_BITS_LOCK: // seed_bits_lock ;; has no affect on
                                           // keywrap?
        data |= s->seed_bits_lock; // don't allow unsetting
        s->seed_bits_lock = data;
        goto jump_log;
    case SEP_AESS_REGISTER_IV ... SEP_AESS_REGISTER_IV + 0xC: // IV
    case 0x100 ... 0x10C: // IV T8015
        memcpy(&s->iv[addr & 0xF], &data, 4);
        goto jump_log;
    case SEP_AESS_REGISTER_IN ... SEP_AESS_REGISTER_IN + 0xC: // IN
    case 0x110 ... 0x11C: // IN T8015
        memcpy(&s->in[addr & 0xF], &data, 4);
        goto jump_log;
    // AES engine?: case 0xA4: 0x40 bytes from TRNG
    default:
    jump_default:
        memcpy(&sep->aess_base_regs[addr], &data, size);
    jump_log:
        DPRINTF("SEP AESS_BASE: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, orig_data);
        break;
    }
}

static uint64_t aess_base_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleAESSState *s = opaque;
    AppleSEPState *sep = s->sep;
    uint64_t ret = 0;

    // QEMU_LOCK_GUARD(&s->lock);

#ifdef ENABLE_CPU_DUMP_STATE
    DPRINTF("\n");
    cpu_dump_state(CPU(sep->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case SEP_AESS_REGISTER_STATUS: // Status
        WITH_QEMU_LOCK_GUARD(&s->lock)
        {
            ret = s->status;
        }
        goto jump_log;
    case SEP_AESS_REGISTER_COMMAND: // Command
        ret = s->command;
        goto jump_log;
    case SEP_AESS_REGISTER_INTERRUPT_STATUS: // Interrupt Status
        WITH_QEMU_LOCK_GUARD(&s->lock)
        {
            ret = s->interrupt_status;
        }
        goto jump_log;
    case SEP_AESS_REGISTER_INTERRUPT_ENABLED: // Interrupt Enabled
        ret = s->interrupt_enabled;
        goto jump_log;
    case SEP_AESS_REGISTER_0x14_KEYWRAP_ITERATIONS_COUNTER:
        ret = s->reg_0x14_keywrap_iterations_counter;
        goto jump_log;
    case SEP_AESS_REGISTER_0x18_KEYDISABLE:
        ret = s->reg_0x18_keydisable;
        goto jump_log;
    case SEP_AESS_REGISTER_SEED_BITS: // seed_bits
        ret = s->seed_bits;
        goto jump_log;
    case SEP_AESS_REGISTER_SEED_BITS_LOCK: // seed_bits_lock
        ret = s->seed_bits_lock;
        goto jump_log;
    case SEP_AESS_REGISTER_IV ... SEP_AESS_REGISTER_IV + 0xC: // IV
        ////case 0x100 ... 0x10C: // IV T8015 ; is this also being read?
        memcpy(&ret, &s->iv[addr & 0xF], 4);
        goto jump_log;
    case SEP_AESS_REGISTER_IN ... SEP_AESS_REGISTER_IN + 0xC: // IN
        ////case 0x110 ... 0x11C: // IN T8015 ; is this also being read?
        memcpy(&ret, &s->in[addr & 0xF], 4);
        goto jump_log;
    case SEP_AESS_REGISTER_TAG_OUT ... SEP_AESS_REGISTER_TAG_OUT +
        0xC: // TAG OUT
        memcpy(&ret, &s->tag_out[addr & 0xF], 4);
        goto jump_log;
    case SEP_AESS_REGISTER_OUT ... SEP_AESS_REGISTER_OUT + 0xC: // OUT
        memcpy(&ret, &s->out[addr & 0xF], 4);
        goto jump_log;
    case 0xE4: // ????
        ret = 0x0;
        goto jump_log;
    case 0x280: // ????
        ret = 0x1;
        goto jump_log;
    default:
    jump_default:
        memcpy(&ret, &sep->aess_base_regs[addr], size);
    jump_log:
        DPRINTF("SEP AESS_BASE: Unknown read at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, ret);
        break;
    }

    return ret;
}

static const MemoryRegionOps aess_base_reg_ops = {
    .write = aess_base_reg_write,
    .read = aess_base_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};

static void aesh_base_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x8: // cmd
        // should be the same as AESS, just with different commands.
        // 0x2/0x18/0x1/0x8/0x10/0xc/0x1c/0x0/0x4/0x9
        goto jump_default;
    // case 0xB4: 0x40 bytes from TRNG
    default:
    jump_default:
        memcpy(&s->aesh_base_regs[addr], &data, size);
        DPRINTF("SEP AESH_BASE: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t aesh_base_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    // from misc0: 0xC, 0xF4
    case 0xC: // ???? bit1 clear, bit0 set
        return (0 << 1) | (1 << 0);
    case 0xF4: // ????
        return 0x0;
    default:
        memcpy(&ret, &s->aesh_base_regs[addr], size);
        DPRINTF("SEP AESH_BASE: Unknown read at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, ret);
        break;
    }

    return ret;
}

static const MemoryRegionOps aesh_base_reg_ops = {
    .write = aesh_base_reg_write,
    .read = aesh_base_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};

static void aesc_base_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&s->aesc_base_regs[addr], &data, size);
        DPRINTF("SEP AESC_BASE: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t aesc_base_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&ret, &s->aesc_base_regs[addr], size);
        DPRINTF("SEP AESC_BASE: Unknown read at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, ret);
        break;
    }

    return ret;
}

static const MemoryRegionOps aesc_base_reg_ops = {
    .write = aesc_base_reg_write,
    .read = aesc_base_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};

static void pka_handle_cmd(ApplePKAState *s)
{
    AppleSEPState *sep = s->sep;

    // values: 0x4/0x8/0x10/0x20/0x40/0x80/0x100
    if (s->command == 0x40) { // migrate data with PKA
        apple_a7iop_interrupt_status_push(APPLE_A7IOP(sep)->iop_mailbox,
                                          0x1000A); // ack first interrupt/0xA
        // apple_a7iop_interrupt_status_push(APPLE_A7IOP(sep)->iop_mailbox,
        // 0x1000B); // ack second interrupt/0xB
        apple_a7iop_interrupt_status_push(APPLE_A7IOP(sep)->iop_mailbox,
                                          0x1000C); // ack third interrupt/0xC
    } else if (s->command == 0x80) { // MPKA_ECPUB_ATTEST
        apple_a7iop_interrupt_status_push(APPLE_A7IOP(sep)->iop_mailbox,
                                          0x1000A); // ack first interrupt/0xA
        // apple_a7iop_interrupt_status_push(APPLE_A7IOP(sep)->iop_mailbox,
        // 0x1000B); // ack second interrupt/0xB
        apple_a7iop_interrupt_status_push(APPLE_A7IOP(sep)->iop_mailbox,
                                          0x1000C); // ack third interrupt/0xC
    }
}

static void pka_handle_cmd_bh(void *opaque)
{
    ApplePKAState *s = opaque;
    pka_handle_cmd(s);
}

static void pka_base_reg_write(void *opaque, hwaddr addr, uint64_t data,
                               unsigned size)
{
    ApplePKAState *s = opaque;
    AppleSEPState *sep = s->sep;

    QEMU_LOCK_GUARD(&s->lock);

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(sep->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x0: // maybe command
        s->command = data;
        // PKA commands get executed directly, without additional trigger
        pka_handle_cmd(s);
        // qemu_bh_schedule(s->command_bh);
        goto jump_log;
    case 0x4: // maybe status_out0
#if 1
        s->status0 = data;
        // maybe use & instead of ==
        if (s->status0 == 0x1) {
            // ack interrupt 0xA
            s->status_in0 = 1;
        } else if (s->status0 == 0x2) {
            // ack interrupt 0xB
            // unknown
        } else if (s->status0 == 0x4) {
            // ack interrupt 0xC
            // unknown
        }
#endif
        goto jump_log;
    case 0x40: // img4out DGST locked
        s->img4out_dgst_locked |= (data & 1);
        goto jump_log;
    case 0x60 ... 0x7C: // img4out DGST data
        if (!s->img4out_dgst_locked) {
            memcpy(&s->img4out_dgst[addr & 0x1F], &data, 4);
        }
        goto jump_log;
    case 0x80 ... 0x9C: // some data
        goto jump_log;
    case 0x800: // chip revision locked
        s->chip_revision_locked |= (data & 1);
        goto jump_log;
    case 0x820: // chip revision data
        if (!s->chip_revision_locked) {
            s->chip_revision = data;
        }
        goto jump_log;
    case 0x840: // ecid chipid misc locked
        s->ecid_chipid_misc_locked |= (data & 1);
        goto jump_log;
    case 0x860 ... 0x870: // ecid chipid misc data ; 0x860/0x864 ecid, 0x870
                          // chipid
        if (!s->ecid_chipid_misc_locked) {
            memcpy(&s->ecid_chipid_misc[(addr & 0x1F) >> 2], &data, 4);
        }
        goto jump_log;
    default:
    jump_default:
        memcpy(&sep->pka_base_regs[addr], &data, size);
    jump_log:
        DPRINTF("SEP PKA_BASE: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t pka_base_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    ApplePKAState *s = opaque;
    AppleSEPState *sep = s->sep;
    uint64_t ret = 0;

    QEMU_LOCK_GUARD(&s->lock);

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(sep->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x8: // maybe status_in0/interrupt_status
#if 1
        // if (s->status0 == 0x1)
        if (s->status_in0 == 0x1) {
            ret = 0x1; // this means mod_PKA_read output ready
        }
#endif
#if 1
        ret = s->status_in0;
        if (s->status_in0 == 1) {
            s->status_in0 = 0;
        }
#endif
        goto jump_log;
    case 0x40: // img4out DGST locked
        ret = s->img4out_dgst_locked;
        goto jump_log;
    case 0x60 ... 0x7C: // img4out DGST data
        memcpy(&ret, &s->img4out_dgst[addr & 0x1F], 4);
        goto jump_log;
    case 0x800: // chip revision locked
        ret = s->chip_revision_locked;
        goto jump_log;
    case 0x820: // chip revision data
        ret = s->chip_revision;
        goto jump_log;
    case 0x840: // ecid chipid misc locked
        ret = s->ecid_chipid_misc_locked;
        goto jump_log;
    case 0x860 ... 0x870: // ecid chipid misc data
        memcpy(&ret, &s->ecid_chipid_misc[(addr & 0x1F) >> 2], 4);
        // memcpy(&ret, &s->ecid_chipid_misc + (addr & 0x1F), 4);
        goto jump_log;
    default:
    jump_default:
        memcpy(&ret, &sep->pka_base_regs[addr], size);
    jump_log:
        DPRINTF("SEP PKA_BASE: Unknown read at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, ret);
        break;
    }

    return ret;
}

static const MemoryRegionOps pka_base_reg_ops = {
    .write = pka_base_reg_write,
    .read = pka_base_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};

static void pka_tmm_reg_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x818 ... 0x834: // some data
        // correct?
        goto jump_log;
    default:
    jump_default:
        memcpy(&s->pka_tmm_regs[addr], &data, size);
    jump_log:
        DPRINTF("SEP PKA_TMM: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t pka_tmm_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x818 ... 0x834:
        // TODO
        goto jump_log;
    default:
    jump_default:
        memcpy(&ret, &s->pka_tmm_regs[addr], size);
    jump_log:
        DPRINTF("SEP PKA_TMM: Unknown read at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, ret);
        break;
    }

    return ret;
}

static const MemoryRegionOps pka_tmm_reg_ops = {
    .write = pka_tmm_reg_write,
    .read = pka_tmm_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};

static void misc2_reg_write(void *opaque, hwaddr addr, uint64_t data,
                            unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    // Some engine?: case 0x28: 0x8 bytes from TRNG
    default:
        memcpy(&s->misc2_regs[addr], &data, size);
        DPRINTF("SEP MISC2: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t misc2_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x24: // ????
        return 0x0;
    default:
        memcpy(&ret, &s->misc2_regs[addr], size);
        DPRINTF("SEP MISC2: Unknown read at 0x" HWADDR_FMT_plx "\n", addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps misc2_reg_ops = {
    .write = misc2_reg_write,
    .read = misc2_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};

static void boot_monitor_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                   unsigned size)
{
    AppleSEPState *s = opaque;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x04: // some status flag, bit0
        data &= ~(1 << 0); // reset bit0 for read
        QEMU_FALLTHROUGH;
    case 0x08: // maybe some command0?
    case 0x10: // maybe some command1?
    case 0x20: // load address low
    case 0x24: // load address high
    case 0x28: // end address low
    case 0x2C: // end address high
    case 0x30: // unknown1 address low
    case 0x34: // unknown1 address high
    case 0x38: // unknown2 address low
    case 0x3C: // unknown2 address high
    case 0x40: // unknown0 address low
    case 0x44: // unknown0 address high
    case 0x48: // randomness low
    case 0x4C: // randomness high
               // goto jump_default;
    default:
        DPRINTF("SEP Boot Monitor: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        memcpy(&s->boot_monitor_regs[addr], &data, size);
        break;
    }
}

static uint64_t boot_monitor_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x04: // some status flag, bit0
        goto jump_default;
    case 0x0C: // must return 0x0
        ret = 0x0;
        return ret;
    default:
        DPRINTF("SEP Boot Monitor: Unknown read at 0x" HWADDR_FMT_plx "\n",
                addr);
    jump_default:
        memcpy(&ret, &s->boot_monitor_regs[addr], size);
        break;
    }

    return ret;
}

static const MemoryRegionOps boot_monitor_reg_ops = {
    .write = boot_monitor_reg_write,
    .read = boot_monitor_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};

static void apple_sep_send_message(AppleSEPState *s, uint8_t ep, uint8_t tag,
                                   uint8_t op, uint8_t param, uint32_t data)
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
    ////apple_a7iop_send_ap(a7iop, sent_msg);
    apple_a7iop_send_iop(a7iop, sent_msg);
}

static void progress_reg_write(void *opaque, hwaddr addr, uint64_t data,
                               unsigned size)
{
    AppleSEPState *s = opaque;
    SEPMessage sep_msg = { 0 };

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    case 0x4:
        // iBoot would send those requests. iOS warns about the
        // responses, because it doesn't expect them.
        if (data == 0xF2E31133) {
            apple_sep_send_message(s, 0xFF, 0x67, 3, 0x00, 0x00);
            DPRINTF("SEP Progress: Sent fake GenerateNonce\n");
            // we have no damn idea what this opcode is, but if tz0
            // isn't large enough compared to the value derived from this data
            // it whines. this value is for t8030, straight from the decompiler.
            apple_sep_send_message(s, 0xFF, 0x0, 17, 0x00, 0x8000);
            DPRINTF("SEP Progress: Sent fake Opcode17\n");
        } else if ((data == 0xFC4A2CAC || data == 0xEEE6BA79) &&
                   (s->chip_id >= 0x8020)) // Enable Trace Buffer
        {
#ifdef SEP_ENABLE_TRACE_BUFFER
            // Only works for >= T8020 here, because the T8015 SEPOS is
            // compressed.
            enable_trace_buffer(s);
#endif
        }
        break;
    case 0x8:
#ifdef SEP_DISABLE_ASLR
        if (data == 0x23BFDFE7) {
            disable_aslr(s);
        }
#endif
        if (data == 0x41A7 && (s->chip_id >= 0x8015)) {
            DPRINTF("%s: SEPFW_copy_test0: 0x" HWADDR_FMT_plx " 0x%" PRIX64
                    "\n",
                    __func__, s->sep_fw_addr, s->sep_fw_size);
#ifdef SEP_ENABLE_HARDCODED_FIRMWARE
            AddressSpace *nsas = &address_space_memory;
            address_space_write(nsas, s->sep_fw_addr, MEMTXATTRS_UNSPECIFIED,
                                s->fw_data, s->sep_fw_size);
#endif
            // g_free(sep_fw);
        }
#if 1
        // if (data == 0x6A5D128D && (s->chip_id == 0x8015))
        if (data == 0x6A5D128D) {
            AppleA7IOPMessage *msg =
                apple_a7iop_inbox_peek(APPLE_A7IOP(s)->iop_mailbox);
            if (msg != NULL) {
                memcpy(&sep_msg, msg->data, sizeof(sep_msg));
                uint64_t shmbuf_base = (uint64_t)sep_msg.data << 12;
                DPRINTF("%s: SHMBUF_TEST0: trace_data8:0x%" PRIX64 ": "
                        "shmbuf=0x" HWADDR_FMT_plx
                        ": ep=0x%02x, tag=0x%02x, opcode=0x%02x(%u), "
                        "param=0x%02x, data=0x%08x\n",
                        APPLE_A7IOP(s)->iop_mailbox->role, data, shmbuf_base,
                        sep_msg.ep, sep_msg.tag, sep_msg.op, sep_msg.op,
                        sep_msg.param, sep_msg.data);
                int debug_trace_mmio_index = -1;
                if (s->chip_id == 0x8015) {
                    debug_trace_mmio_index = 11;
                } else if (s->chip_id >= 0x8020) {
                    debug_trace_mmio_index = 14;
                }
                if (debug_trace_mmio_index != -1) {
                    s->shmbuf_base = shmbuf_base;
                    uint64_t tracebuf_mmio_addr =
                        shmbuf_base + s->trace_buffer_base_offset;
                    DPRINTF("%s: SHMBUF_TEST1: tracbuf=0x" HWADDR_FMT_plx "\n",
                            APPLE_A7IOP(s)->iop_mailbox->role,
                            tracebuf_mmio_addr);
                    // _if SEP_ENABLE_DEBUG_TRACE_MAPPING
                    // TODO: T8020 isn't handled here anymore, but T8015
                    // probably still should.
                    // _endif
                }
            }
        }
#endif
        if (data == 0x23BFDFE7 && (s->chip_id == 0x8015)) {
#define LVL3_BASE_COPYFROM 0x24090C000ULL
            AddressSpace *nsas = &address_space_memory;
            uint64_t pagetable_val = 0;
            for (uint64_t page_addr = 0x340000000ULL;
                 page_addr < 0x342000000ULL; page_addr += 0x4000) {
                pagetable_val = page_addr | 0x603;
                address_space_write(nsas,
                                    LVL3_BASE_COPYFROM +
                                        (((page_addr >> 14) & 0x7FF) * 8),
                                    MEMTXATTRS_UNSPECIFIED, &pagetable_val,
                                    sizeof(pagetable_val));
            }
        }
        break;
    case 0x0:
        memcpy(&s->progress_regs[addr], &data, size);
        DPRINTF("SEP Progress: Progress_0 write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        if (data == 0xDEADBEE0) {
            qemu_irq_lower(qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
        }
        if (data == 0xDEADBEE1) {
            qemu_irq_lower(qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ));
        }
        if (data == 0xDEADBEE2) {
            qemu_irq_lower(qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_VIRQ));
        }
        if (data == 0xDEADBEE3) {
            qemu_irq_lower(qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_VFIQ));
        }

        if (data == 0xDEADBEE4) {
            qemu_irq_raise(qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
        }
        if (data == 0xDEADBEE5) {
            qemu_irq_raise(qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ));
        }
        if (data == 0xDEADBEE6) {
            qemu_irq_raise(qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_VIRQ));
        }
        if (data == 0xDEADBEE7) {
            qemu_irq_raise(qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_VFIQ));
        }
        if (data == 0xCAFE1334) {
            uint32_t i = 0;
            for (i = 0x10000; i < 0x10200; i++) {
                if (i == 0x10008 || i == 0x1002C) {
                    continue;
                }
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  i);
            }
        }
        if (data == 0xCAFE1335) {
            uint32_t i = 0;
            for (i = 0x40000; i < 0x40100; i++) {
                if (i == 0x40000) {
                    continue;
                }
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  i);
            }
        }
        if (data == 0xCAFE1336) {
            uint32_t i = 0;
            for (i = 0x70000; i < 0x70400; i++) {
                // if (i == 0x70001) {
                //     continue;
                // }
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  i);
            }
        }
        if (data == 0xCAFE1337) {
            uint32_t i = 0;
            for (i = 0x10000; i < 0x10200; i++) {
                if (i == 0x10008 || i == 0x1002C) {
                    continue;
                }
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  i);
            }
            for (i = 0x40000; i < 0x40100; i++) {
                if (i == 0x40000) {
                    continue;
                }
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  i);
            }
            for (i = 0x70000; i < 0x70400; i++) {
                // if (i == 0x70001) {
                //     continue;
                // }
                apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox,
                                                  i);
            }
        }
        break;
    case 0x3370:
        memcpy(&s->progress_regs[addr], &data, size);
        DPRINTF("SEP Progress: Progress_1 write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        // apple_mbox_set_custom0(s->mbox, data);
        apple_a7iop_interrupt_status_push(APPLE_A7IOP(s)->iop_mailbox, data);
        break;
    // case 0x4:
    // case 0x8:
    case 0x114:
    case 0x214:
    case 0x218:
    case 0x21C:
    case 0x220:
    case 0x2D8:
    case 0x2DC:
    case 0x2E0: // ecid low
    case 0x2E4: // ecid high
    case 0x2E8: // board-id
    case 0x2EC: // chip-id
    case 0x314:
    case 0x318:
    case 0x31C:
        memcpy(&s->progress_regs[addr], &data, size);
        break;
    default:
        // jump_default:
        memcpy(&s->progress_regs[addr], &data, size);
        DPRINTF("SEP Progress: Unknown write at 0x" HWADDR_FMT_plx
                " with value 0x%" PRIX64 "\n",
                addr, data);
        break;
    }
}

static uint64_t progress_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSEPState *s = opaque;
    uint64_t ret = 0;

#ifdef ENABLE_CPU_DUMP_STATE
    cpu_dump_state(CPU(s->cpu), stderr, CPU_DUMP_CODE);
#endif
    switch (addr) {
    default:
        memcpy(&ret, &s->progress_regs[addr], size);
        DPRINTF("SEP Progress: Unknown read at 0x" HWADDR_FMT_plx "\n", addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps progress_reg_ops = {
    .write = progress_reg_write,
    .read = progress_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.unaligned = false,
};

static void apple_sep_cpu_moni_jump(CPUState *cpu, run_on_cpu_data data)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);

    DPRINTF("%s: before cpu_set_pc: base=0x%" VADDR_PRIX "\n", __func__,
            data.target_ptr);
    cpu_set_pc(cpu, data.target_ptr);

    // possible workaround for intermittent sep boot errors
    if (tcg_enabled()) {
        arm_rebuild_hflags(&arm_cpu->env);
        tlb_flush(cpu);
        tb_flush(cpu);
        smp_wmb();
    }
}

static void apple_sep_iop_start(AppleA7IOP *s)
{
    // some race conditions might happen before, during and/or after the jump.
    AppleSEPState *sep;

    sep = APPLE_SEP(s);

    trace_apple_sep_iop_start(s->iop_mailbox->role);

    hwaddr load_addr = *(hwaddr *)&sep->boot_monitor_regs[0x20];
    // Don't prevent two successive calls of this, it is actually intended,
    // once by seprom and once by sepfw.
    // rely on apple_a7iop_set_cpu_ctrl for "(cpu_ctrl & SEP_BOOT_MONITOR_RUN)"
    if (sep->modern && load_addr != 0) {
        DPRINTF("%s: have load_addr 0x" HWADDR_FMT_plx "\n", __func__,
                load_addr);
        async_run_on_cpu(CPU(sep->cpu), apple_sep_cpu_moni_jump,
                         RUN_ON_CPU_TARGET_PTR(load_addr));
    }
}

static void apple_sep_iop_wakeup(AppleA7IOP *s)
{
    AppleSEPState *sep;

    sep = APPLE_SEP(s);

    trace_apple_sep_iop_wakeup(s->iop_mailbox->role);

    // TODO
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented", __func__);
}

static const AppleA7IOPOps apple_sep_iop_ops = {
    .start = apple_sep_iop_start,
    .wakeup = apple_sep_iop_wakeup,
};

AppleSEPState *apple_sep_from_node(AppleDTNode *node, MemoryRegion *ool_mr,
                                   vaddr base, uint32_t cpu_id, bool modern,
                                   uint32_t chip_id)
{
    DeviceState *dev;
    AppleA7IOP *a7iop;
    AppleSEPState *s;
    SysBusDevice *sbd;
    AppleDTProp *prop;
    uint64_t *reg;
    uint32_t i;

    dev = qdev_new(TYPE_APPLE_SEP);
    a7iop = APPLE_A7IOP(dev);
    s = APPLE_SEP(dev);
    sbd = SYS_BUS_DEVICE(dev);

    prop = apple_dt_get_prop(node, "reg");
    g_assert_nonnull(prop);
    reg = (uint64_t *)prop->data;

    apple_a7iop_init(a7iop, "SEP", reg[1],
                     modern ? APPLE_A7IOP_V4 : APPLE_A7IOP_V2,
                     &apple_sep_iop_ops, NULL);
    s->base = base;
    s->modern = modern;
    s->chip_id = chip_id;

    if (s->chip_id >= 0x8020) {
        if (s->chip_id == 0x8020) {
            g_assert_not_reached();
        }
        s->shmbuf_base = SEP_SHMBUF_BASE;
        s->trace_buffer_base_offset = 0x10000;
        s->debug_trace_size = 0x10000;
    } else if (s->chip_id == 0x8015) {
        s->shmbuf_base = 0; // is dynamic
        s->trace_buffer_base_offset = 0x10000; // ???
        s->debug_trace_size = 0x10000; // ???
    } else if (s->chip_id == 0x8000) {
        s->shmbuf_base = 0; // is dynamic ???
        s->trace_buffer_base_offset = 0x10000; // ???
        s->debug_trace_size = 0x10000; // ???
    } else {
        g_assert_not_reached();
    }

    MemoryRegion *mr0 = g_new0(MemoryRegion, 1);
    memory_region_init_alias(mr0, OBJECT(s), "sep_dma", ool_mr, 0,
                             SEP_DMA_MAPPING_SIZE);
    if (modern) {
        s->cpu = ARM_CPU(apple_a13_create("sep-cpu", cpu_id, 0, -1, 'P'));
        memory_region_add_subregion(&APPLE_A13(s->cpu)->memory, 0, mr0);
    } else {
        s->cpu = ARM_CPU(apple_a9_create("sep-cpu", cpu_id, 0));
        object_property_set_bool(OBJECT(s->cpu), "aarch64", false, NULL);
        unset_feature(&s->cpu->env, ARM_FEATURE_AARCH64);
        memory_region_add_subregion(&APPLE_A9(s->cpu)->memory, 0, mr0);
    }
    if (s->chip_id >= 0x8020) {
        // hack to make SEP_ENABLE_OVERWRITE_SHMBUF_OBJECTS work properly
        MemoryRegion *mr1 = g_new0(MemoryRegion, 1);
        memory_region_init_alias(mr1, OBJECT(s), "sep_shmbuf_hdr", ool_mr,
                                 s->shmbuf_base, 0x4000);
        memory_region_add_subregion(get_system_memory(), s->shmbuf_base, mr1);
    }
    object_property_set_uint(OBJECT(s->cpu), "rvbar", s->base & ~0xFFF, NULL);
    object_property_add_child(OBJECT(dev), DEVICE(s->cpu)->id, OBJECT(s->cpu));

    // AKF_MBOX reg is handled using the device tree
    // XPRT_{PMSC,FUSE,MISC} regs are handled in t8030.c
    memory_region_init_io(&s->pmgr_base_mr, OBJECT(dev), &pmgr_base_reg_ops, s,
                          "sep.pmgr_base", PMGR_BASE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->pmgr_base_mr);
    memory_region_init_io(&s->trng_regs_mr, OBJECT(dev), &trng_regs_reg_ops,
                          &s->trng_state, "sep.trng_regs", TRNG_REGS_REG_SIZE);
    sysbus_init_mmio(sbd, &s->trng_regs_mr);
    memory_region_init_io(&s->key_base_mr, OBJECT(dev), &key_base_reg_ops, s,
                          "sep.key_base", KEY_BASE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->key_base_mr);
    memory_region_init_io(&s->key_fkey_mr, OBJECT(dev), &key_fkey_reg_ops, s,
                          "sep.key_fkey", KEY_FKEY_REG_SIZE_T8015);
    sysbus_init_mmio(sbd, &s->key_fkey_mr);
    memory_region_init_io(&s->key_fcfg_mr, OBJECT(dev), &key_fcfg_reg_ops, s,
                          "sep.key_fcfg", KEY_FCFG_REG_SIZE_T8020);
    sysbus_init_mmio(sbd, &s->key_fcfg_mr);
    memory_region_init_io(&s->moni_base_mr, OBJECT(dev), &moni_base_reg_ops, s,
                          "sep.moni_base", MONI_BASE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->moni_base_mr);
    memory_region_init_io(&s->moni_thrm_mr, OBJECT(dev), &moni_thrm_reg_ops, s,
                          "sep.moni_thrm", MONI_THRM_REG_SIZE);
    sysbus_init_mmio(sbd, &s->moni_thrm_mr);
    memory_region_init_io(&s->eisp_base_mr, OBJECT(dev), &eisp_base_reg_ops, s,
                          "sep.eisp_base", EISP_BASE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->eisp_base_mr);
    memory_region_init_io(&s->eisp_hmac_mr, OBJECT(dev), &eisp_hmac_reg_ops, s,
                          "sep.eisp_hmac", EISP_HMAC_REG_SIZE);
    sysbus_init_mmio(sbd, &s->eisp_hmac_mr);
    memory_region_init_io(&s->aess_base_mr, OBJECT(dev), &aess_base_reg_ops,
                          &s->aess_state, "sep.aess_base", AESS_BASE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->aess_base_mr);
    // at least >= t8015 have this (aesh), according to their seprom's, but I'm
    // not sure about s8000
    memory_region_init_io(&s->aesh_base_mr, OBJECT(dev), &aesh_base_reg_ops, s,
                          "sep.aesh_base", AESH_BASE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->aesh_base_mr);
    memory_region_init_io(&s->aesc_base_mr, OBJECT(dev), &aesc_base_reg_ops, s,
                          "sep.aesc_base", AESC_BASE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->aesc_base_mr);
    memory_region_init_io(&s->pka_base_mr, OBJECT(dev), &pka_base_reg_ops,
                          &s->pka_state, "sep.pka_base", PKA_BASE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->pka_base_mr);
    memory_region_init_io(&s->pka_tmm_mr, OBJECT(dev), &pka_tmm_reg_ops, s,
                          "sep.pka_tmm", PKA_TMM_REG_SIZE);
    sysbus_init_mmio(sbd, &s->pka_tmm_mr);
    memory_region_init_io(&s->misc2_mr, OBJECT(dev), &misc2_reg_ops, s,
                          "sep.misc2", MISC2_REG_SIZE);
    sysbus_init_mmio(sbd, &s->misc2_mr);
    memory_region_init_io(&s->progress_mr, OBJECT(dev), &progress_reg_ops, s,
                          "sep.progress", PROGRESS_REG_SIZE);
    sysbus_init_mmio(sbd, &s->progress_mr);
    memory_region_init_io(&s->boot_monitor_mr, OBJECT(dev),
                          &boot_monitor_reg_ops, s, "sep.boot_monitor",
                          BOOT_MONITOR_REG_SIZE);
    sysbus_init_mmio(sbd, &s->boot_monitor_mr);
    // TODO: Let's think about something for T8015
    memory_region_init_io(&s->debug_trace_mr, OBJECT(dev), &debug_trace_reg_ops,
                          s, "sep.debug_trace",
                          s->debug_trace_size); // Debug trace printing
#ifdef SEP_ENABLE_DEBUG_TRACE_MAPPING
    if (s->chip_id >= 0x8020) {
        if (modern) {
            memory_region_add_subregion(&APPLE_A13(s->cpu)->memory,
                                        s->shmbuf_base +
                                            s->trace_buffer_base_offset,
                                        &s->debug_trace_mr);
        } else {
            memory_region_add_subregion(&APPLE_A9(s->cpu)->memory,
                                        s->shmbuf_base +
                                            s->trace_buffer_base_offset,
                                        &s->debug_trace_mr);
        }
    }
#endif

    AppleDTNode *child = apple_dt_get_node(node, "iop-sep-nub");
    g_assert_nonnull(child);

    MachineState *machine = MACHINE(qdev_get_machine());
    SysBusDevice *gpio = NULL;
    uint32_t sep_gpio_pins = 0x4;
    uint32_t sep_gpio_int_groups = 0x1;
    gpio = SYS_BUS_DEVICE(apple_gpio_new("sep_gpio", 0x10000, sep_gpio_pins,
                                         sep_gpio_int_groups));
    g_assert_nonnull(gpio);
    if (s->chip_id == 0x8030) {
        sysbus_mmio_map(gpio, 0, 0x2414C0000ULL); // T8030
    } else if (s->chip_id == 0x8020) {
        sysbus_mmio_map(gpio, 0, 0x241480000ULL); // T8020
    } else if (s->chip_id == 0x8015) {
        sysbus_mmio_map(gpio, 0, 0x240F00000ULL); // T8015
    } else if (s->chip_id == 0x8000) {
        sysbus_mmio_map(gpio, 0, 0x20DF00000ULL); // S8000
    }
    s->aess_state.chip_id = s->chip_id;

    s->trng_state.sep = s;
    s->aess_state.sep = s;
    s->pka_state.sep = s;

    for (i = 0; i < sep_gpio_int_groups; i++) {
        // sysbus_connect_irq(gpio, i,
        // qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
    }
    for (i = 0; i < sep_gpio_pins; i++) {
        // qdev_connect_gpio_out(DEVICE(gpio), i,
        // qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
    }
    object_property_add_child(OBJECT(machine), "sep_gpio", OBJECT(gpio));
    sysbus_realize_and_unref(gpio, &error_fatal);
    SysBusDevice *i2c = NULL;
    i2c = apple_i2c_create("sep_i2c");
    g_assert_nonnull(i2c);
    object_property_add_child(OBJECT(machine), "sep_i2c", OBJECT(i2c));
    if (s->chip_id == 0x8030) {
        sysbus_mmio_map(i2c, 0, 0x241480000ULL); // T8030
    } else if (s->chip_id == 0x8020) {
        sysbus_mmio_map(i2c, 0, 0x241440000ULL); // T8020
    } else if (s->chip_id == 0x8015) {
        sysbus_mmio_map(i2c, 0, 0x240700000ULL); // T8015
    } else if (s->chip_id == 0x8000) {
        sysbus_mmio_map(i2c, 0, 0x20D700000ULL); // S8000
    }
    sysbus_realize_and_unref(i2c, &error_fatal);
    uint64_t nvram_size = 64 * KiB;

    DriveInfo *dinfo_eeprom = drive_get_by_index(IF_PFLASH, 0);
    g_assert_nonnull(dinfo_eeprom);
    BlockBackend *blk_eeprom = blk_by_legacy_dinfo(dinfo_eeprom);
    g_assert_nonnull(blk_eeprom);
    I2CSlave *nvram = at24c_eeprom_init_rom_blk(
        APPLE_I2C(i2c)->bus, 0x51, nvram_size, NULL, 0, 2, blk_eeprom);
    g_assert_nonnull(nvram);
    s->nvram = nvram;
    if (s->chip_id >= 0x8020) {
        DriveInfo *dinfo_ssc = drive_get_by_index(IF_PFLASH, 1);
        g_assert_nonnull(dinfo_ssc);
        BlockBackend *blk_ssc = blk_by_legacy_dinfo(dinfo_ssc);
        g_assert_nonnull(blk_ssc);
        AppleSSCState *ssc = apple_ssc_create(machine, 0x71);
        g_assert_nonnull(ssc);
        s->ssc_state = ssc;
        s->ssc_state->aess_state = &s->aess_state;
        qdev_prop_set_drive_err(DEVICE(s->ssc_state), "drive", blk_ssc,
                                &error_fatal);
        blk_set_perm(blk_ssc, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                     BLK_PERM_ALL, &error_fatal);
    }

#if 1
    s->ool_mr = ool_mr;
    g_assert_nonnull(s->ool_mr);
    g_assert_nonnull(
        object_property_add_const_link(OBJECT(s), "ool-mr", OBJECT(s->ool_mr)));
    s->ool_as = g_new0(AddressSpace, 1);
    g_assert_nonnull(s->ool_as);
    address_space_init(s->ool_as, s->ool_mr, "sep.ool");
#endif

    qemu_mutex_init(&s->aess_state.lock);
    qemu_mutex_init(&s->pka_state.lock);

    // No async necessary for TRNG?
    s->aess_state.command_bh =
        aio_bh_new_guarded(qemu_get_aio_context(), aess_handle_cmd_bh,
                           &s->aess_state, &DEVICE(s)->mem_reentrancy_guard);
    s->pka_state.command_bh = aio_bh_new_guarded(
        qemu_get_aio_context(), pka_handle_cmd_bh, &s->pka_state,
        &DEVICE(s)->mem_reentrancy_guard); // unused yet

    return s;
}

static void apple_sep_cpu_reset_work(CPUState *cpu, run_on_cpu_data data)
{
    AppleSEPState *s = data.host_ptr;
    cpu_reset(cpu);
    DPRINTF("apple_sep_cpu_reset_work: before cpu_set_pc: base=0x%" VADDR_PRIX
            "\n",
            s->base);
    cpu_set_pc(cpu, s->base);
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
    qdev_realize(DEVICE(s->cpu), NULL, errp);
    qdev_connect_gpio_out_named(DEVICE(APPLE_A7IOP(s)->iop_mailbox),
                                APPLE_A7IOP_SEP_CPU_IRQ, 0,
                                qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
    // mailbox irq's aren't being handled that way (anymore)
    // timer0 == phys
    qdev_connect_gpio_out(
        DEVICE(s->cpu), GTIMER_PHYS,
        qdev_get_gpio_in_named(DEVICE(APPLE_A7IOP(s)->iop_mailbox),
                               APPLE_A7IOP_SEP_GPIO_TIMER0, 0));
    // timer1 == virt (sepos >= 16)
    qdev_connect_gpio_out(
        DEVICE(s->cpu), GTIMER_VIRT,
        qdev_get_gpio_in_named(DEVICE(APPLE_A7IOP(s)->iop_mailbox),
                               APPLE_A7IOP_SEP_GPIO_TIMER1, 0));
}

static void aess_reset(AppleAESSState *s)
{
    s->status = 0;
    s->command = 0;
    s->interrupt_status = 0;
    s->interrupt_enabled = 0;
    s->reg_0x14_keywrap_iterations_counter = 0;
    s->reg_0x18_keydisable = 0;
    s->seed_bits = 0;
    s->seed_bits_lock = 0;
    //
    s->keywrap_uid0_enabled = false;
    s->keywrap_uid1_enabled = false;
    memset(s->keywrap_key_uid0, 0, sizeof(s->keywrap_key_uid0));
    memset(s->keywrap_key_uid1, 0, sizeof(s->keywrap_key_uid1));
    memset(s->custom_key_index, 0, sizeof(s->custom_key_index));
    memset(s->custom_key_index_enabled, 0, sizeof(s->custom_key_index_enabled));
}

static void pka_reset(ApplePKAState *s)
{
    s->command = 0;
    s->status0 = 0;
    s->status_in0 = 0;
    s->img4out_dgst_locked = 0;
    s->chip_revision_locked = 0;
    s->ecid_chipid_misc_locked = 0;
    s->chip_revision = 0;
    memset(s->img4out_dgst, 0, sizeof(s->img4out_dgst));
    memset(s->output0, 0, sizeof(s->output0));
    memset(s->input0, 0, sizeof(s->input0));
    memset(s->public_key, 0, sizeof(s->public_key));
    memset(s->attest_hash, 0, sizeof(s->attest_hash));
    memset(s->input1, 0, sizeof(s->input1));
    memset(s->ecid_chipid_misc, 0, sizeof(s->ecid_chipid_misc));
}


static void map_sepfw(AppleSEPState *s)
{
    DPRINTF("%s: entered function\n", __func__);
    AddressSpace *nsas = &address_space_memory;
    // Apparently needed because of a bug occurring on XNU
    // clear lowest 0x4000 bytes as well, because they shouldn't contain any
    // valid data
    address_space_set(nsas, 0x0, 0, SEPFW_MAPPING_SIZE, MEMTXATTRS_UNSPECIFIED);
#ifdef SEP_ENABLE_HARDCODED_FIRMWARE
    address_space_rw(nsas, 0x4000ULL, MEMTXATTRS_UNSPECIFIED,
                     (uint8_t *)s->fw_data, s->sep_fw_size, true);
#endif
}

static void apple_sep_reset_hold(Object *obj, ResetType type)
{
    AppleSEPState *s;
    AppleSEPClass *sc;

    s = APPLE_SEP(obj);
    sc = APPLE_SEP_GET_CLASS(obj);

    if (sc->parent_phases.hold != NULL) {
        sc->parent_phases.hold(obj, type);
    }
    s->key_fcfg_offset_0x14_index = 0;
    memset(s->key_fcfg_offset_0x14_values, 0,
           sizeof(s->key_fcfg_offset_0x14_values));
    s->pmgr_fuse_changer_bit0_was_set = false;
    s->pmgr_fuse_changer_bit1_was_set = false;
    memset(s->pmgr_base_regs, 0, sizeof(s->pmgr_base_regs));
    memset(s->key_base_regs, 0, sizeof(s->key_base_regs));
    memset(s->key_fkey_regs, 0, sizeof(s->key_fkey_regs));
    memset(s->key_fcfg_regs, 0, sizeof(s->key_fcfg_regs));
    memset(s->moni_base_regs, 0, sizeof(s->moni_base_regs));
    memset(s->moni_thrm_regs, 0, sizeof(s->moni_thrm_regs));
    memset(s->eisp_base_regs, 0, sizeof(s->eisp_base_regs));
    memset(s->eisp_hmac_regs, 0, sizeof(s->eisp_hmac_regs));
    memset(s->aess_base_regs, 0, sizeof(s->aess_base_regs));
    memset(s->aesh_base_regs, 0, sizeof(s->aesh_base_regs));
    memset(s->aesc_base_regs, 0, sizeof(s->aesc_base_regs));
    memset(s->pka_base_regs, 0, sizeof(s->pka_base_regs));
    memset(s->pka_tmm_regs, 0, sizeof(s->pka_tmm_regs));
    memset(s->misc2_regs, 0, sizeof(s->misc2_regs));
    memset(s->boot_monitor_regs, 0, sizeof(s->boot_monitor_regs));
    memset(s->progress_regs, 0, sizeof(s->progress_regs));
    memset(s->debug_trace_regs, 0, sizeof(s->debug_trace_regs));

    aess_reset(&s->aess_state);
    pka_reset(&s->pka_state);
    // apple_ssc_reset is being called, but not here.
    run_on_cpu(CPU(s->cpu), apple_sep_cpu_reset_work, RUN_ON_CPU_HOST_PTR(s));
    map_sepfw(s);
}

static void apple_sep_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    AppleSEPClass *sc = APPLE_SEP_CLASS(klass);
    device_class_set_parent_realize(dc, apple_sep_realize, &sc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, apple_sep_reset_hold, NULL,
                                       &sc->parent_phases);
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

static int apple_ssc_event(I2CSlave *s, enum i2c_event event)
{
    AppleSSCState *ssc = APPLE_SSC(s);

    switch (event) {
    case I2C_START_SEND:
        DPRINTF("apple_ssc_event: I2C_START_SEND\n");
        break;
    case I2C_FINISH:
        DPRINTF("apple_ssc_event: I2C_FINISH\n");
        break;
    case I2C_START_RECV:
        DPRINTF("apple_ssc_event: I2C_START_RECV\n");
        break;
    case I2C_NACK:
        DPRINTF("apple_ssc_event: I2C_NACK\n");
        break;
    default:
        return -1;
    }
    return 0;
}

#define SSC_REQUEST_SIZE_CMD_0x0 (0x84)
#define SSC_REQUEST_SIZE_CMD_0x1 (0x74)
#define SSC_REQUEST_SIZE_CMD_0x2 (0x4)
#define SSC_REQUEST_SIZE_CMD_0x3 (0x34)
#define SSC_REQUEST_SIZE_CMD_0x4 (0x14)
#define SSC_REQUEST_SIZE_CMD_0x5 (0x54)
#define SSC_REQUEST_SIZE_CMD_0x6 (0x14)
#define SSC_REQUEST_SIZE_CMD_0x7 (0x4)
#define SSC_REQUEST_SIZE_CMD_0x8 (0x4)
#define SSC_REQUEST_SIZE_CMD_0x9 (0x4)

#define SSC_RESPONSE_SIZE_CMD_0x0 (0xC4)
#define SSC_RESPONSE_SIZE_CMD_0x1 (0x74)
#define SSC_RESPONSE_SIZE_CMD_0x2 (0x4)
#define SSC_RESPONSE_SIZE_CMD_0x3 (0x14)
#define SSC_RESPONSE_SIZE_CMD_0x4 (0x54)
#define SSC_RESPONSE_SIZE_CMD_0x5 (0x14)
#define SSC_RESPONSE_SIZE_CMD_0x6 (0x34)
#define SSC_RESPONSE_SIZE_CMD_0x7 (0x78)
#define SSC_RESPONSE_SIZE_CMD_0x8 (0x4)
#define SSC_RESPONSE_SIZE_CMD_0x9 (0x2F)

static uint8_t ssc_request_sizes[] = {
    SSC_REQUEST_SIZE_CMD_0x0, SSC_REQUEST_SIZE_CMD_0x1,
    SSC_REQUEST_SIZE_CMD_0x2, SSC_REQUEST_SIZE_CMD_0x3,
    SSC_REQUEST_SIZE_CMD_0x4, SSC_REQUEST_SIZE_CMD_0x5,
    SSC_REQUEST_SIZE_CMD_0x6, SSC_REQUEST_SIZE_CMD_0x7,
    SSC_REQUEST_SIZE_CMD_0x8, SSC_REQUEST_SIZE_CMD_0x9
};

static uint8_t INFOSTR_AKE_SESSIONSEED[] = "AKE_SessionSeed\n";
static uint8_t INFOSTR_AKE_MACKEY[] = "AKE_MACKey\n\n\n\n\n\n";
static uint8_t INFOSTR_AKE_EXTRACTORKEY[] = "AKE_ExtractorKey";

static bool is_keyslot_valid(struct AppleSSCState *ssc_state,
                             uint8_t kbkdf_index)
{
    bool ret;

    ret = !buffer_is_zero(&ssc_state->ecc_keys[kbkdf_index],
                          sizeof(struct ecc_scalar));
    ret &= !buffer_is_zero(&ssc_state->kbkdf_keys[kbkdf_index],
                           sizeof(ssc_state->kbkdf_keys[kbkdf_index]));

    DPRINTF("%s: kbkdf_index: %d ; ecc_keys_item_size: 0x%lX ; "
            "kbkdf_keys_item_size: 0x%lX\n",
            __func__, kbkdf_index, sizeof(struct ecc_scalar),
            sizeof(ssc_state->kbkdf_keys[kbkdf_index]));
    return ret;
}

static int aes_ccm_crypt(struct AppleSSCState *ssc_state, uint8_t kbkdf_index,
                         uint8_t *prefix, int payload_len, uint8_t *data,
                         uint8_t *out, int encrypt, int response_key)
{
    struct ccm_aes256_ctx aes;
    uint32_t counter_be = cpu_to_be32(ssc_state->kbkdf_counter[kbkdf_index]);
    uint8_t nonce[AES_CCM_NONCE_LENGTH] = { 0 };
    uint8_t auth[AES_CCM_AUTH_LENGTH] = { 0 };
    uint8_t tmp_in[AES_CCM_MAX_DATA_LENGTH] = { 0 };
    uint8_t tmp_out[AES_CCM_MAX_DATA_LENGTH] = { 0 };
    uint8_t *key = NULL;
    int status = 0;
#if 0
    // SEPFW role
    if (encrypt) {
        key = &ssc_state->kbkdf_keys[kbkdf_index][KBKDF_KEY_REQUEST_KEY_OFFSET];
        ssc_state->kbkdf_counter[kbkdf_index]++;
    } else {
        key = &ssc_state->kbkdf_keys[kbkdf_index][KBKDF_KEY_RESPONSE_KEY_OFFSET];
    }
#endif
#if 1
    // SSC role
    // if (encrypt)
    if (response_key) {
        key =
            &ssc_state->kbkdf_keys[kbkdf_index][KBKDF_KEY_RESPONSE_KEY_OFFSET];
    } else {
        key = &ssc_state->kbkdf_keys[kbkdf_index][KBKDF_KEY_REQUEST_KEY_OFFSET];
        ssc_state->kbkdf_counter[kbkdf_index]++;
    }
#endif

    memcpy(auth, prefix, MSG_PREFIX_LENGTH);
    memcpy(&auth[MSG_PREFIX_LENGTH], &counter_be, AES_CCM_COUNTER_LENGTH);
    memcpy(nonce, &ssc_state->kbkdf_keys[kbkdf_index][KBKDF_KEY_SEED_OFFSET],
           KBKDF_KEY_SEED_LENGTH);
    memcpy(&nonce[KBKDF_KEY_SEED_LENGTH], &counter_be, AES_CCM_COUNTER_LENGTH);
    ccm_aes256_set_key(&aes, key);
    if (encrypt) {
        ccm_aes256_encrypt_message(
            &aes, AES_CCM_NONCE_LENGTH, nonce, AES_CCM_AUTH_LENGTH, auth,
            AES_CCM_TAG_LENGTH, AES_CCM_TAG_LENGTH + payload_len, tmp_out,
            data);
        // data[0x20]-tag[0x10] => tag[0x10]-data[0x20]
        memcpy(out, &tmp_out[payload_len], AES_CCM_TAG_LENGTH);
        memcpy(&out[AES_CCM_TAG_LENGTH], tmp_out, payload_len);
    } else {
        DPRINTF("counter_be: 0x%08x\n", counter_be);
        // tag[0x10]-data[0x20] => data[0x20]-tag[0x10]
        memcpy(tmp_in, &data[AES_CCM_TAG_LENGTH], payload_len);
        memcpy(&tmp_in[payload_len], data, AES_CCM_TAG_LENGTH);
        HEXDUMP("tmp_in__tag_plus_encdata", data,
                AES_CCM_TAG_LENGTH + payload_len);
        HEXDUMP("tmp_in__encdata_plus_tag", tmp_in,
                AES_CCM_TAG_LENGTH + payload_len);
        status = ccm_aes256_decrypt_message(
            &aes, AES_CCM_NONCE_LENGTH, nonce, AES_CCM_AUTH_LENGTH, auth,
            AES_CCM_TAG_LENGTH, payload_len, tmp_out, tmp_in);
        if (!status) {
            DPRINTF("%s: ccm_aes256_decrypt_message: DIGEST INVALID\n",
                    __func__);
        }
        memcpy(out, tmp_out, payload_len);
    }
    ////memcpy(out, tmp_out, AES_CCM_MAX_DATA_LENGTH);
    return status;
}

static int aes_cmac_prefix_public(uint8_t *key, uint8_t *prefix,
                                  uint8_t *public0, uint8_t *digest)
{
    struct cmac_aes256_ctx ctx;
    cmac_aes256_set_key(&ctx, key);
    cmac_aes256_update(&ctx, MSG_PREFIX_LENGTH, prefix);
    cmac_aes256_update(&ctx, SECP384_PUBLIC_XY_SIZE, public0);
    cmac_aes256_digest(&ctx, CMAC128_DIGEST_SIZE, digest);
    return 0;
}

static int aes_cmac_prefix_public_public(uint8_t *key, uint8_t *prefix,
                                         uint8_t *public0, uint8_t *public1,
                                         uint8_t *digest)
{
    struct cmac_aes256_ctx ctx;
    cmac_aes256_set_key(&ctx, key);
    cmac_aes256_update(&ctx, MSG_PREFIX_LENGTH, prefix);
    cmac_aes256_update(&ctx, SECP384_PUBLIC_XY_SIZE, public0);
    cmac_aes256_update(&ctx, SECP384_PUBLIC_XY_SIZE, public1);
    cmac_aes256_digest(&ctx, CMAC128_DIGEST_SIZE, digest);
    return 0;
}

static int kbkdf_generate_key(uint8_t *cmac_key, uint8_t *label,
                              uint8_t *context, uint8_t *derived, int length)
{
    struct cmac_aes256_ctx ctx;

    uint8_t digest[CMAC128_DIGEST_SIZE] = { 0 };

    int counter = 1;
    uint16_t be_len = cpu_to_be16(length * 8);
    uint8_t zero = 0;
    cmac_aes256_set_key(&ctx, cmac_key);

    for (size_t i = 0; i < length; i += CMAC128_DIGEST_SIZE) {
        uint16_t be_cnt = 0;
        be_cnt = cpu_to_be16(counter);
        cmac_aes256_update(&ctx, KBKDF_CMAC_LENGTH_SIZE, (uint8_t *)&be_cnt);
        cmac_aes256_update(&ctx, KBKDF_CMAC_LABEL_SIZE, label); // 0x10 bytes
        cmac_aes256_update(&ctx, 1, (uint8_t *)&zero);
        cmac_aes256_update(&ctx, KBKDF_CMAC_CONTEXT_SIZE, context); // 4 bytes
        cmac_aes256_update(&ctx, KBKDF_CMAC_LENGTH_SIZE, (uint8_t *)&be_len);
        cmac_aes256_digest(&ctx, CMAC128_DIGEST_SIZE, digest);
        memcpy(&derived[i], digest, MIN(CMAC128_DIGEST_SIZE, length - i));
        counter++;
    }

    return 0;
}

static int generate_ec_priv(struct AppleSSCState *ssc_state, const char *priv,
                            struct ecc_scalar *ecc_key,
                            struct ecc_point *ecc_pub)
{
    const struct ecc_curve *ecc = nettle_get_secp_384r1();
    mpz_t temp1;

    ecc_point_init(ecc_pub, ecc);
    ecc_scalar_init(ecc_key, ecc);

    if (priv == NULL) {
        ecdsa_generate_keypair(ecc_pub, ecc_key, &ssc_state->rctx,
                               (nettle_random_func *)knuth_lfib_random);
    } else {
        mpz_init_set_str(temp1, priv, 16);
        mpz_add_ui(temp1, temp1, 1);
        if (ecc_scalar_set(ecc_key, temp1) == 0) {
            mpz_clear(temp1);
            return -1;
        }
        mpz_clear(temp1);
        ecc_point_mul_g(ecc_pub, ecc_key);
    }

    return 0;
}

static int output_ec_pub(struct ecc_point *ecc_pub, uint8_t *pub_xy)
{
    // const struct ecc_curve *ecc = nettle_get_secp_384r1();
    mpz_t temp1, temp2;

    mpz_inits(temp1, temp2, NULL);
    ecc_point_get(ecc_pub, temp1, temp2);
    mpz_export(&pub_xy[0x00], NULL, 1, 1, 1, 0, temp1);
    mpz_export(&pub_xy[0x00 + BYTELEN_384], NULL, 1, 1, 1, 0, temp2);
    HEXDUMP("output_ec_pub: pub_x", &pub_xy[0x00], BYTELEN_384);
    HEXDUMP("output_ec_pub: pub_y", &pub_xy[0x00 + BYTELEN_384], BYTELEN_384);

    mpz_clears(temp1, temp2, NULL);

    return 0;
}

static int input_ec_pub(struct ecc_point *ecc_pub, uint8_t *pub_xy)
{
    const struct ecc_curve *ecc = nettle_get_secp_384r1();
    mpz_t temp1, temp2;
    int ret = 0;

    HEXDUMP("input_ec_pub: pub_x", &pub_xy[0x00], BYTELEN_384);
    HEXDUMP("input_ec_pub: pub_y", &pub_xy[0x00 + BYTELEN_384], BYTELEN_384);
    mpz_inits(temp1, temp2, NULL);
    mpz_import(temp1, BYTELEN_384, 1, 1, 1, 0, &pub_xy[0x00]);
    mpz_import(temp2, BYTELEN_384, 1, 1, 1, 0, &pub_xy[0x00 + BYTELEN_384]);
    ecc_point_init(ecc_pub, ecc);
    ret = ecc_point_set(ecc_pub, temp1, temp2);

    mpz_clears(temp1, temp2, NULL);

    return ret;
}

static int generate_kbkdf_keys(struct AppleSSCState *ssc_state,
                               struct ecc_scalar *ecc_key,
                               struct ecc_point *ecc_pub_peer,
                               uint8_t *hmac_key, uint8_t *label,
                               uint8_t *context, uint8_t kbkdf_index)
{
    const struct ecc_curve *ecc = nettle_get_secp_384r1();
    struct ecc_point T;
    // shared_key == pub_x (first half)
    uint8_t shared_key_xy[SECP384_PUBLIC_XY_SIZE] = { 0 };
    uint8_t derived_key[SHA256_DIGEST_SIZE] = { 0 };
    DPRINTF("generate_kbkdf_keys: label: %s\n", label); // 0x10 bytes
    DPRINTF("generate_kbkdf_keys: context: %02x%02x%02x%02x\n", context[0x00],
            context[0x01], context[0x02],
            context[0x03]); // 4 bytes

    ecc_point_init(&T, ecc);
    ecc_point_mul(&T, ecc_key, ecc_pub_peer);
    DPRINTF("generate_kbkdf_keys: shared_key==pub_x:\n");
    output_ec_pub(&T, shared_key_xy);
    ecc_point_clear(&T);

    struct hmac_sha256_ctx ctx;
    hmac_sha256_set_key(&ctx, SHA256_DIGEST_SIZE, hmac_key);
    // only the first half is the shared_key
    hmac_sha256_update(&ctx, BYTELEN_384, shared_key_xy);
    hmac_sha256_digest(&ctx, SHA256_DIGEST_SIZE, derived_key);
    HEXDUMP("generate_kbkdf_keys: derived_key", derived_key,
            SHA256_DIGEST_SIZE);

    int err = kbkdf_generate_key(derived_key, label, context,
                                 ssc_state->kbkdf_keys[kbkdf_index],
                                 KBKDF_CMAC_OUTPUT_LEN);
    if (err) {
        DPRINTF("error: kbkdf_generate_key returned non-zero\n");
        return err;
    }
    ssc_state->kbkdf_counter[kbkdf_index] = 0;
    HEXDUMP("generate_kbkdf_keys: ssc_state->kbkdf_keys[kbkdf_index]",
            ssc_state->kbkdf_keys[kbkdf_index], KBKDF_CMAC_OUTPUT_LEN);

    return 0;
}

static void hkdf_sha256(int salt_len, uint8_t *salt, int info_len,
                        uint8_t *info, int key_len, uint8_t *key, uint8_t *out)
{
    struct hmac_sha256_ctx ctx;
    uint8_t prk[SHA256_DIGEST_SIZE];

    hmac_sha256_set_key(&ctx, salt_len, salt);
    hkdf_extract(&ctx, (nettle_hash_update_func *)hmac_sha256_update,
                 (nettle_hash_digest_func *)hmac_sha256_digest,
                 SHA256_DIGEST_SIZE, key_len, key, prk);

    hmac_sha256_set_key(&ctx, SHA256_DIGEST_SIZE, prk);
    hkdf_expand(&ctx, (nettle_hash_update_func *)hmac_sha256_update,
                (nettle_hash_digest_func *)hmac_sha256_digest,
                SHA256_DIGEST_SIZE, info_len, info, SHA256_DIGEST_SIZE, out);
}

static void aes_keys_from_sp_key(struct AppleSSCState *ssc_state,
                                 uint8_t kbkdf_index, uint8_t *prefix,
                                 uint8_t *aes_key_mackey,
                                 uint8_t *aes_key_extractorkey)
{
    // wrapping with "SP key"/"Spes"/"Lynx version 1 crypto" could be wrong.
    uint8_t hmac_key[0x20] = { 0 };
    memcpy(hmac_key, ssc_state->slot_hmac_key[kbkdf_index], 0x20);
    HEXDUMP("aes_keys_from_sp_key: hmac_key", hmac_key, 0x20);
    kbkdf_generate_key(hmac_key, INFOSTR_AKE_MACKEY, prefix, aes_key_mackey,
                       0x20);
    HEXDUMP("aes_keys_from_sp_key: aes_key_mackey", aes_key_mackey, 0x20);
    kbkdf_generate_key(hmac_key, INFOSTR_AKE_EXTRACTORKEY, prefix,
                       aes_key_extractorkey, 0x20);
    HEXDUMP("aes_keys_from_sp_key: aes_key_extractorkey", aes_key_extractorkey,
            0x20);
}

static void do_response_prefix(uint8_t *request, uint8_t *response,
                               uint8_t flags)
{
    memset(response, 0, SSC_MAX_RESPONSE_SIZE);
    uint8_t cmd = request[0];
    response[0] = cmd;
    if (cmd <= 0x6) {
        response[1] = request[1];
    }
    response[2] = 0;
    response[3] = flags;
}

static void clear_ecc_scalar(struct ecc_scalar *ecc_key)
{
    if (!buffer_is_zero(ecc_key, sizeof(struct ecc_scalar))) {
        ecc_scalar_clear(ecc_key);
        memset(ecc_key, 0, sizeof(*ecc_key));
    }
}

// TODO: Properly handle various error cases with cmd 0x0/0x1/..., like wrong
// hashes/signatures/parameters or public keys not being on the curve.

static void answer_cmd_0x0_init1(struct AppleSSCState *ssc_state,
                                 uint8_t *request, uint8_t *response)
{
    DPRINTF("%s: entered function\n", __func__);
    struct ecc_point cmd0_ecpub, ecc_pub;
    struct dsa_signature signature;
    uint8_t digest[BYTELEN_384] = { 0 };
    uint8_t kbkdf_index = 0; // hardcoded
    struct sha384_ctx ctx;

    dsa_signature_init(&signature);

    if (is_keyslot_valid(ssc_state, kbkdf_index)) { // shouldn't already exist
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid kbkdf_index: %u\n",
                      __func__, kbkdf_index);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_KEYSLOT_INVALID);
        return;
    }
    if (input_ec_pub(&cmd0_ecpub,
                     &request[MSG_PREFIX_LENGTH + SHA256_DIGEST_SIZE]) ==
        0) { // curve is invalid
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid curve\n", __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CURVE_INVALID);
        goto jump_ret1;
    }
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    const char *priv_str = "222222222222222222222222222222222222222222222222"
                           "222222222222222222222222222222222222222222222222";
    if (generate_ec_priv(ssc_state, priv_str, &ssc_state->ecc_keys[kbkdf_index],
                         &ecc_pub) != 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: generate_ec_priv failed\n",
                      __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CURVE_INVALID);
        goto jump_ret0;
    }
    output_ec_pub(&ecc_pub,
                  &response[MSG_PREFIX_LENGTH + SECP384_PUBLIC_XY_SIZE]);
    memcpy(ssc_state->random_hmac_key, &request[MSG_PREFIX_LENGTH],
           SHA256_DIGEST_SIZE);
    DPRINTF("INFOSTR_AKE_SESSIONSEED: %s\n", INFOSTR_AKE_SESSIONSEED);
    generate_kbkdf_keys(ssc_state, &ssc_state->ecc_keys[kbkdf_index],
                        &cmd0_ecpub, ssc_state->random_hmac_key,
                        INFOSTR_AKE_SESSIONSEED, request, kbkdf_index);

    sha384_init(&ctx);
    sha384_update(&ctx, MSG_PREFIX_LENGTH, &response[0x00]); // prefix
    sha384_update(
        &ctx, SECP384_PUBLIC_XY_SIZE,
        &request[MSG_PREFIX_LENGTH + SHA256_DIGEST_SIZE]); // sw_public_xy0
    sha384_update(
        &ctx, SECP384_PUBLIC_XY_SIZE,
        &response[MSG_PREFIX_LENGTH + SECP384_PUBLIC_XY_SIZE]); // public_xy1
    sha384_update(&ctx, SHA256_DIGEST_SIZE,
                  ssc_state->random_hmac_key); // hmac_key
    sha384_digest(&ctx, BYTELEN_384, digest);
    HEXDUMP("answer_cmd_0x0_init1 digest", digest, BYTELEN_384);
    // Using non-deterministic signing here like it's probably supposed to be.
    // Don't want to implement/port deterministic signing.
    ecdsa_sign(&ssc_state->ecc_key_main, &ssc_state->rctx,
               (nettle_random_func *)knuth_lfib_random, BYTELEN_384, digest,
               &signature);
    mpz_export(&response[MSG_PREFIX_LENGTH + 0x00 + 0x00], NULL, 1, 1, 1, 0,
               signature.r);
    mpz_export(&response[MSG_PREFIX_LENGTH + 0x00 + BYTELEN_384], NULL, 1, 1, 1,
               0, signature.s);
    dsa_signature_clear(&signature);
jump_ret0:
    ecc_point_clear(&ecc_pub);
jump_ret1:
    ecc_point_clear(&cmd0_ecpub);
}

static void answer_cmd_0x1_connect_sp(struct AppleSSCState *ssc_state,
                                      uint8_t *request, uint8_t *response)
{
    DPRINTF("%s: entered function\n", __func__);
    HEXDUMP("cmd_0x01_req", request, SSC_REQUEST_SIZE_CMD_0x1);
    struct ecc_point cmd1_ecpub, ecc_pub;
    uint8_t kbkdf_index = request[1];

    uint8_t *cmac_req_should = &request[MSG_PREFIX_LENGTH];
    uint8_t *sw_public_xy2 = &request[MSG_PREFIX_LENGTH + AES_BLOCK_SIZE];
    DPRINTF("answer_cmd_0x1_connect_sp: kbkdf_index: %u\n", kbkdf_index);
    HEXDUMP("answer_cmd_0x1_connect_sp: req_prefix", request,
            MSG_PREFIX_LENGTH);
    HEXDUMP("answer_cmd_0x1_connect_sp: sw_public_xy2", sw_public_xy2,
            SECP384_PUBLIC_XY_SIZE);
    HEXDUMP("answer_cmd_0x1_connect_sp: cmac_req_should", cmac_req_should,
            AES_BLOCK_SIZE);
    if (kbkdf_index >= KBKDF_KEY_MAX_SLOTS) {
        DPRINTF("%s: kbkdf_index over limit: %u\n", __func__, kbkdf_index);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_COMMAND_OR_FIELD_INVALID);
        return;
    }
    if (is_keyslot_valid(ssc_state, kbkdf_index)) { // shouldn't already exist
        DPRINTF("%s: invalid kbkdf_index: %u\n", __func__, kbkdf_index);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_KEYSLOT_INVALID);
        return;
    }
    if (input_ec_pub(&cmd1_ecpub, sw_public_xy2) == 0) { // curve is invalid
        DPRINTF("%s: invalid curve\n", __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CURVE_INVALID);
        goto jump_ret1;
    }
    const char *priv_str = "333333333333333333333333333333333333333333333333"
                           "333333333333333333333333333333333333333333333333";
    if (generate_ec_priv(ssc_state, priv_str, &ssc_state->ecc_keys[kbkdf_index],
                         &ecc_pub) != 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: generate_ec_priv failed\n",
                      __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CURVE_INVALID);
        goto jump_ret0;
    }
    uint8_t aes_key_mackey_req[0x20] = { 0 };
    uint8_t aes_key_extractorkey_req[0x20] = { 0 };
    aes_keys_from_sp_key(ssc_state, kbkdf_index, request, aes_key_mackey_req,
                         aes_key_extractorkey_req);
    uint8_t cmac_req_is[AES_BLOCK_SIZE] = { 0 };
    aes_cmac_prefix_public(aes_key_mackey_req, request, sw_public_xy2,
                           cmac_req_is);
    HEXDUMP("answer_cmd_0x1_connect_sp: aes_key_mackey_req", aes_key_mackey_req,
            sizeof(aes_key_mackey_req));
    HEXDUMP("answer_cmd_0x1_connect_sp: aes_key_extractorkey_req ",
            aes_key_extractorkey_req, sizeof(aes_key_extractorkey_req));
    HEXDUMP("answer_cmd_0x1_connect_sp: cmac_req_is", cmac_req_is,
            sizeof(cmac_req_is));
    if (memcmp(cmac_req_should, cmac_req_is, sizeof(cmac_req_is)) != 0) {
        DPRINTF("%s: invalid CMAC\n", __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CMAC_INVALID);
        goto jump_ret0;
    }
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    output_ec_pub(&ecc_pub, &response[MSG_PREFIX_LENGTH + AES_BLOCK_SIZE]);
    generate_kbkdf_keys(ssc_state, &ssc_state->ecc_keys[kbkdf_index],
                        &cmd1_ecpub, aes_key_extractorkey_req,
                        INFOSTR_AKE_SESSIONSEED, request, kbkdf_index);

    uint8_t *cmac_resp = &response[MSG_PREFIX_LENGTH];
    uint8_t *public_xy2 = &response[MSG_PREFIX_LENGTH + AES_BLOCK_SIZE];
    aes_cmac_prefix_public_public(aes_key_mackey_req, response, sw_public_xy2,
                                  public_xy2, cmac_resp);

    HEXDUMP("cmd_0x01_resp", response, SSC_RESPONSE_SIZE_CMD_0x1);
jump_ret0:
    ecc_point_clear(&ecc_pub);
jump_ret1:
    ecc_point_clear(&cmd1_ecpub);
}

static void answer_cmd_0x2_disconnect_sp(struct AppleSSCState *ssc_state,
                                         uint8_t *request, uint8_t *response)
{
    DPRINTF("%s: entered function\n", __func__);
    HEXDUMP("cmd_0x02_req", request, SSC_REQUEST_SIZE_CMD_0x2);
    uint8_t kbkdf_index = request[1];
    if (!is_keyslot_valid(ssc_state, kbkdf_index)) { // should already exist
        DPRINTF("%s: invalid kbkdf_index: %u\n", __func__, kbkdf_index);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_KEYSLOT_INVALID);
        return;
    }
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    clear_ecc_scalar(&ssc_state->ecc_keys[kbkdf_index]);
    memset(&ssc_state->kbkdf_keys[kbkdf_index], 0,
           sizeof(ssc_state->kbkdf_keys[kbkdf_index]));
    ssc_state->kbkdf_counter[kbkdf_index] = 0;
    DPRINTF("answer_cmd_0x2_disconnect_sp: kbkdf_index: %u\n", kbkdf_index);
}

static void answer_cmd_0x3_metadata_write(struct AppleSSCState *ssc_state,
                                          uint8_t *request, uint8_t *response)
{
    DPRINTF("%s: entered function\n", __func__);
    HEXDUMP("cmd_0x03_req", request, SSC_REQUEST_SIZE_CMD_0x3);
    uint8_t kbkdf_index_key = request[1];
    uint8_t kbkdf_index_dataslot = request[2];
    uint8_t copy = request[3];
    DPRINTF("cmd_0x03_req: kbkdf_index_key: %u\n", kbkdf_index_key);
    DPRINTF("cmd_0x03_req: kbkdf_index_dataslot: %u\n", kbkdf_index_dataslot);
    DPRINTF("cmd_0x03_req: copy: %u\n", copy);
    ////if (copy >= SSC_REQUEST_MAX_COPIES)
    if (copy > 0) {
        DPRINTF("%s: invalid copy: %u\n", __func__, copy);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_COMMAND_OR_FIELD_INVALID);
        return;
    }
    if (kbkdf_index_key >= KBKDF_KEY_MAX_SLOTS ||
        !is_keyslot_valid(ssc_state, kbkdf_index_key)) {
        DPRINTF("%s: invalid kbkdf_index_key: %u\n", __func__, kbkdf_index_key);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_KEYSLOT_INVALID);
        return;
    }
    if (kbkdf_index_dataslot == 0 ||
        kbkdf_index_dataslot >= KBKDF_KEY_MAX_SLOTS) {
        DPRINTF("%s: invalid kbkdf_index_dataslot: %u\n", __func__,
                kbkdf_index_dataslot);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_KEYSLOT_INVALID);
        return;
    }
    int blk_offset = (kbkdf_index_dataslot * CMD_METADATA_DATA_PAYLOAD_LENGTH *
                      SSC_REQUEST_MAX_COPIES) +
                     (copy * CMD_METADATA_DATA_PAYLOAD_LENGTH);
    int key_offset =
        (KBKDF_KEY_KEY_FILE_OFFSET * CMD_METADATA_DATA_PAYLOAD_LENGTH *
         SSC_REQUEST_MAX_COPIES) +
        (kbkdf_index_dataslot * KBKDF_KEY_KEY_LENGTH);
    DPRINTF("cmd_0x03_req: blk_offset: 0x%X\n", blk_offset);
    HEXDUMP("cmd_0x03_req: ssc_state->kbkdf_keys[kbkdf_index_key]",
            ssc_state->kbkdf_keys[kbkdf_index_key], KBKDF_CMAC_OUTPUT_LEN);

    uint8_t req_dec_out[CMD_METADATA_PAYLOAD_LENGTH] = { 0 };
    int err0 = aes_ccm_crypt(
        ssc_state, kbkdf_index_key, &request[0x00], CMD_METADATA_PAYLOAD_LENGTH,
        &request[MSG_PREFIX_LENGTH], req_dec_out, false, false);
    if (err0 == 0) {
        DPRINTF("%s: invalid CMAC\n", __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CMAC_INVALID);
        return;
    }
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    HEXDUMP("cmd_0x03_req: req_dec_out", req_dec_out,
            CMD_METADATA_PAYLOAD_LENGTH);

    memcpy(ssc_state->slot_hmac_key[kbkdf_index_dataslot], req_dec_out,
           sizeof(req_dec_out)); // 0x20 bytes ; necessary here because there
                                 // are no metadata reads (cmd 0x6) after that.

    // blk_pwrite(ssc_state->blk, blk_offset, CMD_METADATA_PAYLOAD_LENGTH,
    // req_dec_out, 0); // Is it really necessary to write the mac_key or any
    // metadata to blk_offset?
    uint8_t zeroes_0x40[CMD_METADATA_DATA_PAYLOAD_LENGTH] = { 0 };
    blk_pwrite(ssc_state->blk, blk_offset, CMD_METADATA_DATA_PAYLOAD_LENGTH,
               zeroes_0x40, 0); // clear it on metadata write, all 0x40 bytes at
                                // blk_offset. is this correct?
    blk_pwrite(ssc_state->blk, key_offset, CMD_METADATA_PAYLOAD_LENGTH,
               req_dec_out, 0);

    uint8_t resp_nop_out[1] = { 0x00 };
    HEXDUMP("cmd_0x03_resp: resp_nop_out", resp_nop_out, 1);
    int err1 =
        aes_ccm_crypt(ssc_state, kbkdf_index_key, &response[0x00], 0x0,
                      resp_nop_out, &response[MSG_PREFIX_LENGTH], true, true);
    HEXDUMP("cmd_0x03_resp", response, SSC_RESPONSE_SIZE_CMD_0x3);
}

static void answer_cmd_0x4_metadata_data_read(struct AppleSSCState *ssc_state,
                                              uint8_t *request,
                                              uint8_t *response)
{
    DPRINTF("%s: entered function\n", __func__);
    HEXDUMP("cmd_0x04_req", request, SSC_REQUEST_SIZE_CMD_0x4);
    uint8_t kbkdf_index = request[1];
    uint8_t copy = request[3];
    DPRINTF("cmd_0x04_req: kbkdf_index: %u\n", kbkdf_index);
    DPRINTF("cmd_0x04_req: copy: %u\n", copy);
    if (copy >= SSC_REQUEST_MAX_COPIES) {
        DPRINTF("%s: invalid copy: %u\n", __func__, copy);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_COMMAND_OR_FIELD_INVALID);
        return;
    }
    if (kbkdf_index == 0 || kbkdf_index >= KBKDF_KEY_MAX_SLOTS ||
        !is_keyslot_valid(ssc_state, kbkdf_index)) {
        DPRINTF("%s: invalid kbkdf_index: %u\n", __func__, kbkdf_index);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_KEYSLOT_INVALID);
        return;
    }
    int blk_offset = (kbkdf_index * CMD_METADATA_DATA_PAYLOAD_LENGTH *
                      SSC_REQUEST_MAX_COPIES) +
                     (copy * CMD_METADATA_DATA_PAYLOAD_LENGTH);
    DPRINTF("cmd_0x04_req: blk_offset: 0x%X\n", blk_offset);
    HEXDUMP("cmd_0x04_req: ssc_state->kbkdf_keys[kbkdf_index]",
            ssc_state->kbkdf_keys[kbkdf_index], KBKDF_CMAC_OUTPUT_LEN);

    uint8_t req_nop_out[1] = { 0 };
    int err0 =
        aes_ccm_crypt(ssc_state, kbkdf_index, &request[0x00], 0x0,
                      &request[MSG_PREFIX_LENGTH], req_nop_out, false, false);
    if (err0 == 0) {
        DPRINTF("%s: invalid CMAC\n", __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CMAC_INVALID);
        return;
    }
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    HEXDUMP("cmd_0x04_req: req_nop_out", req_nop_out, 1);

    uint8_t resp_dec_out[CMD_METADATA_DATA_PAYLOAD_LENGTH] = { 0 };
    blk_pread(ssc_state->blk, blk_offset, CMD_METADATA_DATA_PAYLOAD_LENGTH,
              resp_dec_out, 0);

    HEXDUMP("cmd_0x04_resp: resp_dec_out", resp_dec_out,
            CMD_METADATA_DATA_PAYLOAD_LENGTH);
    int err1 = aes_ccm_crypt(ssc_state, kbkdf_index, &response[0x00],
                             CMD_METADATA_DATA_PAYLOAD_LENGTH, resp_dec_out,
                             &response[MSG_PREFIX_LENGTH], true, true);
    HEXDUMP("cmd_0x04_resp", response, SSC_RESPONSE_SIZE_CMD_0x4);
}

static void answer_cmd_0x5_metadata_data_write(struct AppleSSCState *ssc_state,
                                               uint8_t *request,
                                               uint8_t *response)
{
    DPRINTF("%s: entered function\n", __func__);
    HEXDUMP("cmd_0x05_req", request, SSC_REQUEST_SIZE_CMD_0x5);
    uint8_t kbkdf_index = request[1];
    uint8_t copy = request[3];
    DPRINTF("cmd_0x05_req: kbkdf_index: %u\n", kbkdf_index);
    DPRINTF("cmd_0x05_req: copy: %u\n", copy);
    if (copy >= SSC_REQUEST_MAX_COPIES) {
        DPRINTF("%s: invalid copy: %u\n", __func__, copy);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_COMMAND_OR_FIELD_INVALID);
        return;
    }
    if (kbkdf_index == 0 || kbkdf_index >= KBKDF_KEY_MAX_SLOTS ||
        !is_keyslot_valid(ssc_state, kbkdf_index)) {
        DPRINTF("%s: invalid kbkdf_index: %u\n", __func__, kbkdf_index);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_KEYSLOT_INVALID);
        return;
    }
    int blk_offset = (kbkdf_index * CMD_METADATA_DATA_PAYLOAD_LENGTH *
                      SSC_REQUEST_MAX_COPIES) +
                     (copy * CMD_METADATA_DATA_PAYLOAD_LENGTH);
    DPRINTF("cmd_0x05_req: blk_offset: 0x%X\n", blk_offset);
    HEXDUMP("cmd_0x05_req: ssc_state->kbkdf_keys[kbkdf_index]",
            ssc_state->kbkdf_keys[kbkdf_index], KBKDF_CMAC_OUTPUT_LEN);

    uint8_t req_dec_out[CMD_METADATA_DATA_PAYLOAD_LENGTH] = { 0 };
    int err0 =
        aes_ccm_crypt(ssc_state, kbkdf_index, &request[0x00],
                      CMD_METADATA_DATA_PAYLOAD_LENGTH,
                      &request[MSG_PREFIX_LENGTH], req_dec_out, false, false);
    if (err0 == 0) {
        DPRINTF("%s: invalid CMAC\n", __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CMAC_INVALID);
        return;
    }
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    HEXDUMP("cmd_0x05_req: req_dec_out", req_dec_out,
            CMD_METADATA_DATA_PAYLOAD_LENGTH);

    blk_pwrite(ssc_state->blk, blk_offset, CMD_METADATA_DATA_PAYLOAD_LENGTH,
               req_dec_out, 0);

    uint8_t resp_nop_out[1] = { 0x00 };
    HEXDUMP("cmd_0x05_resp: resp_nop_out", resp_nop_out, 1);
    int err1 =
        aes_ccm_crypt(ssc_state, kbkdf_index, &response[0x00], 0x0,
                      resp_nop_out, &response[MSG_PREFIX_LENGTH], true, true);
    HEXDUMP("cmd_0x05_resp", response, SSC_RESPONSE_SIZE_CMD_0x5);
}

static void answer_cmd_0x6_metadata_read(struct AppleSSCState *ssc_state,
                                         uint8_t *request, uint8_t *response)
{
    DPRINTF("%s: entered function\n", __func__);
    HEXDUMP("cmd_0x06_req", request, SSC_REQUEST_SIZE_CMD_0x6);

    uint8_t kbkdf_index_key = request[1];
    uint8_t kbkdf_index_dataslot = request[2];
    uint8_t copy = request[3];
    DPRINTF("cmd_0x06_req: kbkdf_index_key: %u\n", kbkdf_index_key);
    DPRINTF("cmd_0x06_req: kbkdf_index_dataslot: %u\n", kbkdf_index_dataslot);
    DPRINTF("cmd_0x06_req: copy: %u\n", copy);
    if (copy >= SSC_REQUEST_MAX_COPIES) {
        DPRINTF("%s: invalid copy: %u\n", __func__, copy);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_COMMAND_OR_FIELD_INVALID);
        return;
    }
    if (kbkdf_index_key >= KBKDF_KEY_MAX_SLOTS ||
        !is_keyslot_valid(ssc_state, kbkdf_index_key)) {
        DPRINTF("%s: invalid kbkdf_index_key: %u\n", __func__, kbkdf_index_key);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_KEYSLOT_INVALID);
        return;
    }
    if (kbkdf_index_dataslot == 0 ||
        kbkdf_index_dataslot >= KBKDF_KEY_MAX_SLOTS) {
        DPRINTF("%s: invalid kbkdf_index_dataslot: %u\n", __func__,
                kbkdf_index_dataslot);
        do_response_prefix(request, response,
                           SSC_RESPONSE_FLAG_KEYSLOT_INVALID);
        return;
    }
    int blk_offset = (kbkdf_index_dataslot * CMD_METADATA_DATA_PAYLOAD_LENGTH *
                      SSC_REQUEST_MAX_COPIES) +
                     (copy * CMD_METADATA_DATA_PAYLOAD_LENGTH);
    int key_offset =
        (KBKDF_KEY_KEY_FILE_OFFSET * CMD_METADATA_DATA_PAYLOAD_LENGTH *
         SSC_REQUEST_MAX_COPIES) +
        (kbkdf_index_dataslot * KBKDF_KEY_KEY_LENGTH);
    DPRINTF("cmd_0x06_req: blk_offset: 0x%X\n", blk_offset);
    HEXDUMP("cmd_0x06_req: ssc_state->kbkdf_keys[kbkdf_index_key]",
            ssc_state->kbkdf_keys[kbkdf_index_key], KBKDF_CMAC_OUTPUT_LEN);

    uint8_t req_nop_out[1] = { 0 };
    int err0 =
        aes_ccm_crypt(ssc_state, kbkdf_index_key, &request[0x00], 0x0,
                      &request[MSG_PREFIX_LENGTH], req_nop_out, false, false);
    if (err0 == 0) {
        DPRINTF("%s: invalid CMAC\n", __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CMAC_INVALID);
        return;
    }
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    HEXDUMP("cmd_0x06_req: req_nop_out", req_nop_out, 1);

    uint8_t resp_dec_out[CMD_METADATA_PAYLOAD_LENGTH] = { 0 };
    blk_pread(ssc_state->blk, blk_offset, CMD_METADATA_PAYLOAD_LENGTH,
              resp_dec_out, 0);
    blk_pread(ssc_state->blk, key_offset, CMD_METADATA_PAYLOAD_LENGTH,
              ssc_state->slot_hmac_key[kbkdf_index_dataslot], 0);

    HEXDUMP("cmd_0x06_resp: resp_dec_out", resp_dec_out,
            CMD_METADATA_PAYLOAD_LENGTH);
    int err1 = aes_ccm_crypt(ssc_state, kbkdf_index_key, &response[0x00],
                             CMD_METADATA_PAYLOAD_LENGTH, resp_dec_out,
                             &response[MSG_PREFIX_LENGTH], true, true);
    HEXDUMP("cmd_0x06_resp", response, SSC_RESPONSE_SIZE_CMD_0x6);
}

static void answer_cmd_0x7_init0(struct AppleSSCState *ssc_state,
                                 uint8_t *request, uint8_t *response)
{
    struct ecc_point ecc_pub;
    DPRINTF("%s: entered function\n", __func__);

    const char *priv_str = "111111111111111111111111111111111111111111111111"
                           "111111111111111111111111111111111111111111111111";
    // no NULL here, this should stay static
    if (generate_ec_priv(ssc_state, priv_str, &ssc_state->ecc_key_main,
                         &ecc_pub) != 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: generate_ec_priv failed\n",
                      __func__);
        do_response_prefix(request, response, SSC_RESPONSE_FLAG_CURVE_INVALID);
        goto jump_ret;
    }
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    uint8_t unknown0[0x06] = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB };
    uint8_t cpsn[0x07] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xCC };
    uint8_t unknown1[0x07] = { 0xCD, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05 };
    memcpy(ssc_state->cpsn, cpsn, sizeof(ssc_state->cpsn));
    memcpy(&response[MSG_PREFIX_LENGTH], unknown0, sizeof(unknown0));
    memcpy(&response[MSG_PREFIX_LENGTH + sizeof(unknown0)], ssc_state->cpsn,
           sizeof(ssc_state->cpsn));
    memcpy(&response[MSG_PREFIX_LENGTH + sizeof(unknown0) +
                     sizeof(ssc_state->cpsn)],
           unknown1, sizeof(unknown1));
    output_ec_pub(&ecc_pub,
                  &response[MSG_PREFIX_LENGTH + sizeof(unknown0) +
                            sizeof(ssc_state->cpsn) + sizeof(unknown1)]);

    HEXDUMP("cmd_0x07_resp", response, SSC_RESPONSE_SIZE_CMD_0x7);

jump_ret:
    ecc_point_clear(&ecc_pub);
}

static void answer_cmd_0x8_sleep(struct AppleSSCState *ssc_state,
                                 uint8_t *request, uint8_t *response)
{
    DPRINTF("%s: entered function\n", __func__);
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    HEXDUMP("cmd_0x08_resp", response, SSC_RESPONSE_SIZE_CMD_0x8);
}

static void answer_cmd_0x9_panic(struct AppleSSCState *ssc_state,
                                 uint8_t *request, uint8_t *response)
{
    DPRINTF("%s: entered function\n", __func__);
    ////apple_ssc_reset(DEVICE(ssc_state));
    do_response_prefix(request, response, SSC_RESPONSE_FLAG_OK);
    // uint8_t panic_data[0x24] = {...};
    // memcpy(&response[MSG_PREFIX_LENGTH], panic_data, 0x24);
    memset(&response[MSG_PREFIX_LENGTH], 0xCC, 0x24);
    memcpy(&response[MSG_PREFIX_LENGTH + 0x24], ssc_state->cpsn,
           sizeof(ssc_state->cpsn));
    HEXDUMP("cmd_0x09_resp", response, SSC_RESPONSE_SIZE_CMD_0x9);
}

static uint8_t apple_ssc_rx(I2CSlave *i2c)
{
    AppleSSCState *ssc = APPLE_SSC(i2c);
    uint8_t ret = 0;

    // ssc->req_cur = 0;

    if (ssc->resp_cur >= sizeof(ssc->resp_cmd)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: ssc->resp_cur too high 0x%02x\n",
                      __func__, ssc->resp_cur);
        return 0;
    }

    if (ssc->resp_cur == 0) {
        // ssc->req_cur = 0;
        memset(ssc->resp_cmd, 0, sizeof(ssc->resp_cmd));
        ssc->resp_cmd[0] = ssc->req_cmd[0];
    }
    // This tries to prevent a spurious call during a dummy read.
    if (ssc->resp_cur == 1) {
        uint8_t cmd = ssc->req_cmd[0];
        if (cmd > 0x09) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: cmd %u: invalid command > 0x09",
                          __func__, cmd);
            do_response_prefix(ssc->req_cmd, ssc->resp_cmd,
                               SSC_RESPONSE_FLAG_COMMAND_OR_FIELD_INVALID);
        } else if (ssc->req_cur != ssc_request_sizes[cmd]) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: cmd %u: invalid cmdsize mismatch req_cur "
                          "is 0x%02x != should 0x%02x\n",
                          __func__, cmd, ssc->req_cur, ssc_request_sizes[cmd]);
            do_response_prefix(ssc->req_cmd, ssc->resp_cmd,
                               SSC_RESPONSE_FLAG_COMMAND_SIZE_MISMATCH);
        } else if (cmd == 0x00) { // req 0x84 bytes, resp 0xC4 bytes
            answer_cmd_0x0_init1(ssc, ssc->req_cmd, ssc->resp_cmd);
        } else if (cmd == 0x01) { // req 0x74 bytes, resp 0x74 bytes
            answer_cmd_0x1_connect_sp(ssc, ssc->req_cmd, ssc->resp_cmd);
        } else if (cmd == 0x02) { // req 0x04 bytes, resp 0x04 bytes
            answer_cmd_0x2_disconnect_sp(ssc, ssc->req_cmd, ssc->resp_cmd);
        } else if (cmd == 0x03) { // req 0x34 bytes, resp 0x14 bytes
            answer_cmd_0x3_metadata_write(ssc, ssc->req_cmd, ssc->resp_cmd);
        } else if (cmd == 0x04) { // req 0x14 bytes, resp 0x54 bytes
            answer_cmd_0x4_metadata_data_read(ssc, ssc->req_cmd, ssc->resp_cmd);
        } else if (cmd == 0x05) { // req 0x54 bytes, resp 0x14 bytes
            answer_cmd_0x5_metadata_data_write(ssc, ssc->req_cmd,
                                               ssc->resp_cmd);
        } else if (cmd == 0x06) { // req 0x14 bytes, resp 0x34 bytes
            answer_cmd_0x6_metadata_read(ssc, ssc->req_cmd, ssc->resp_cmd);
        } else if (cmd == 0x07) { // req 0x04 bytes, resp 0x78 bytes
            answer_cmd_0x7_init0(ssc, ssc->req_cmd, ssc->resp_cmd);
        } else if (cmd == 0x08) { // req 0x04 bytes, resp 0x04 bytes
            answer_cmd_0x8_sleep(ssc, ssc->req_cmd, ssc->resp_cmd);
        } else if (cmd == 0x09) { // req 0x04 bytes, resp 0x2F bytes
            answer_cmd_0x9_panic(ssc, ssc->req_cmd, ssc->resp_cmd);
        }
        ssc->req_cur = 0;
        memset(ssc->req_cmd, 0, sizeof(ssc->req_cmd));
        if (ssc->resp_cmd[3] != SSC_RESPONSE_FLAG_OK) {
            memset(&ssc->resp_cmd[MSG_PREFIX_LENGTH], 0xFF,
                   sizeof(ssc->resp_cmd) - MSG_PREFIX_LENGTH);
        }
    }

    ret = ssc->resp_cmd[ssc->resp_cur++];
    DPRINTF("apple_ssc_rx: resp_cur=0x%02x ret=0x%02x\n", ssc->resp_cur - 1,
            ret);
#if 0
    AppleSEPState *sep = ssc->aess_state->sep;
    apple_a7iop_interrupt_status_push(a7iop->iop_mailbox, 0x10002); // I2C
#endif
    return ret;
}

static int apple_ssc_tx(I2CSlave *i2c, uint8_t data)
{
    AppleSSCState *ssc = APPLE_SSC(i2c);

    if (ssc->req_cur == 0) {
        ssc->resp_cur = 0;
        memset(ssc->resp_cmd, 0, sizeof(ssc->resp_cmd));
    }

    if (ssc->req_cur >= sizeof(ssc->req_cmd)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "apple_ssc_tx: ssc->req_cur too high 0x%02x\n",
                      ssc->req_cur);
        return 0;
    }

    DPRINTF("apple_ssc_tx: req_cur=0x%02x data=0x%02x\n", ssc->req_cur, data);
    ssc->req_cmd[ssc->req_cur++] = data;
    return 0;
}

static void apple_ssc_reset(DeviceState *state)
{
    AppleSSCState *ssc = APPLE_SSC(state);
    DPRINTF("%s: called\n", __func__);

    ssc->req_cur = 0;
    ssc->resp_cur = 0;
    memset(ssc->req_cmd, 0, sizeof(ssc->req_cmd));
    memset(ssc->resp_cmd, 0, sizeof(ssc->resp_cmd));

    clear_ecc_scalar(&ssc->ecc_key_main);
    for (int i = 0; i < KBKDF_KEY_MAX_SLOTS; i++) {
        clear_ecc_scalar(&ssc->ecc_keys[i]);
    }
    knuth_lfib_init(&ssc->rctx, 4711);
    memset(ssc->random_hmac_key, 0, sizeof(ssc->random_hmac_key));
    memset(ssc->slot_hmac_key, 0, sizeof(ssc->slot_hmac_key));
    memset(ssc->kbkdf_keys, 0, sizeof(ssc->kbkdf_keys));
    memset(ssc->kbkdf_counter, 0, sizeof(ssc->kbkdf_counter));
    uint8_t cpsn[0x07] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xFE };
    memcpy(ssc->cpsn, cpsn, sizeof(cpsn));
    blk_set_perm(ssc->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                 BLK_PERM_ALL, &error_fatal);
}

AppleSSCState *apple_ssc_create(MachineState *machine, uint8_t addr)
{
    AppleSSCState *ssc;
    AppleI2CState *i2c = APPLE_I2C(
        object_property_get_link(OBJECT(machine), "sep_i2c", &error_fatal));
    ssc = APPLE_SSC(i2c_slave_create_simple(i2c->bus, TYPE_APPLE_SSC, addr));
    return ssc;
}

static const Property apple_ssc_props[] = {
    DEFINE_PROP_DRIVE("drive", AppleSSCState, blk),
};

static void apple_ssc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *c = I2C_SLAVE_CLASS(klass);

    dc->desc = "Apple SSC";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    c->event = apple_ssc_event;
    c->recv = apple_ssc_rx;
    c->send = apple_ssc_tx;
    device_class_set_legacy_reset(dc, apple_ssc_reset);

    device_class_set_props(dc, apple_ssc_props);
}

static const TypeInfo apple_ssc_type_info = {
    .name = TYPE_APPLE_SSC,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(AppleSSCState),
    .class_init = apple_ssc_class_init,
};

static void apple_ssc_register_types(void)
{
    type_register_static(&apple_ssc_type_info);
}

type_init(apple_ssc_register_types);
