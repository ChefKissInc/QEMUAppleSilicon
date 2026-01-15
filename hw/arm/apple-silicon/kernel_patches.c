/*
 * ChefKiss Kernel Patches.
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
#include "hw/arm/apple-silicon/kernel_patches.h"
#include "hw/arm/apple-silicon/patcher.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"

#define NOP (0xD503201F)
#define MOV_W0_0 (0x52800000)
#define NOP_BYTES 0x1F, 0x20, 0x03, 0xD5
#define RET (0xD65F03C0)
#define RETAB (0xD65F0FFF)
#define PACIBSP (0xD503237F)

static CKPatcherRange *ck_kp_range_from_va(const char *name, vaddr base,
                                           vaddr size)
{
    CKPatcherRange *range = g_new0(CKPatcherRange, 1);
    range->addr = base;
    range->length = size;
    range->ptr = apple_boot_va_to_ptr(base);
    range->name = name;
    return range;
}

static CKPatcherRange *ck_kp_find_section_range(MachoHeader64 *hdr,
                                                const char *segment,
                                                const char *section)
{
    MachoSection64 *sec;
    MachoSegmentCommand64 *seg;

    seg = apple_boot_get_segment(hdr, segment);
    if (seg == NULL) {
        return NULL;
    }

    sec = apple_boot_get_section(seg, section);
    return sec == NULL ? NULL :
                         ck_kp_range_from_va(segment, sec->addr, sec->size);
}

// TODO: Fix host endianness BE vs LE troubles in here.
static MachoHeader64 *ck_kp_find_image_header(MachoHeader64 *hdr,
                                              const char *bundle_id)
{
    uint64_t *info;
    uint64_t *start;
    uint32_t count;
    uint32_t i;
    char kname[256] = { 0 };
    const char *prelinkinfo;
    const char *last_dict;

    if (hdr->file_type == MH_FILESET) {
        return apple_boot_get_fileset_header(hdr, bundle_id);
    }

    g_autofree CKPatcherRange *kmod_info_range =
        ck_kp_find_section_range(hdr, "__PRELINK_INFO", "__kmod_info");
    if (kmod_info_range == NULL) {
        g_autofree CKPatcherRange *kext_info_range =
            ck_kp_find_section_range(hdr, "__PRELINK_INFO", "__info");
        if (kext_info_range == NULL) {
            error_report("Unsupported XNU.");
            return NULL;
        }

        prelinkinfo =
            strstr((const char *)kext_info_range->ptr, "PrelinkInfoDictionary");
        last_dict = strstr(prelinkinfo, "<array>") + 7;
        while (last_dict) {
            const char *nested_dict;
            const char *ident;
            const char *end_dict = strstr(last_dict, "</dict>");
            if (!end_dict) {
                break;
            }

            nested_dict = strstr(last_dict + 1, "<dict>");
            while (nested_dict) {
                if (nested_dict > end_dict) {
                    break;
                }

                nested_dict = strstr(nested_dict + 1, "<dict>");
                end_dict = strstr(end_dict + 1, "</dict>");
            }

            ident = g_strstr_len(last_dict, end_dict - last_dict,
                                 "CFBundleIdentifier");
            if (ident != NULL) {
                const char *value = strstr(ident, "<string>");
                if (value != NULL) {
                    const char *value_end;

                    value += strlen("<string>");
                    value_end = strstr(value, "</string>");
                    if (value_end != NULL) {
                        memcpy(kname, value, value_end - value);
                        kname[value_end - value] = 0;
                        if (strcmp(kname, bundle_id) == 0) {
                            const char *addr =
                                g_strstr_len(last_dict, end_dict - last_dict,
                                             "_PrelinkExecutableLoadAddr");
                            if (addr != NULL) {
                                const char *avalue = strstr(addr, "<integer");
                                if (avalue != NULL) {
                                    avalue = strstr(avalue, ">");
                                    if (avalue != NULL) {
                                        return apple_boot_va_to_ptr(
                                            strtoull(++avalue, 0, 0));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            last_dict = strstr(end_dict, "<dict>");
        }

        return NULL;
    }
    g_autofree CKPatcherRange *kmod_start_range =
        ck_kp_find_section_range(hdr, "__PRELINK_INFO", "__kmod_start");
    if (kmod_start_range != NULL) {
        info = (uint64_t *)kmod_info_range->ptr;
        start = (uint64_t *)kmod_start_range->ptr;
        count = kmod_info_range->length / 8;
        for (i = 0; i < count; i++) {
            const char *kext_name =
                (const char *)apple_boot_va_to_ptr(info[i]) + 0x10;
            if (strcmp(kext_name, bundle_id) == 0) {
                return apple_boot_va_to_ptr(start[i]);
            }
        }
    }

    return NULL;
}

static CKPatcherRange *ck_kp_find_image_text(MachoHeader64 *hdr,
                                             const char *bundle_id)
{
    hdr = ck_kp_find_image_header(hdr, bundle_id);
    return hdr == NULL ? NULL :
                         ck_kp_find_section_range(hdr, "__TEXT_EXEC", "__text");
}

static CKPatcherRange *ck_kp_get_kernel_section(MachoHeader64 *hdr,
                                                const char *segment,
                                                const char *section)
{
    if (hdr->file_type == MH_FILESET) {
        MachoHeader64 *kernel =
            ck_kp_find_image_header(hdr, "com.apple.kernel");
        return kernel == NULL ?
                   NULL :
                   ck_kp_find_section_range(kernel, segment, section);
    }

    return ck_kp_find_section_range(hdr, segment, section);
}

static void ck_kp_apfs_patches(CKPatcherRange *range)
{
    static const uint8_t root_auth[] = {
        0x68, 0x00, 0x28, 0x37, // tbnz w8, 5, 0xC
        0X00, 0x0A, 0x80, 0x52, // mov w0, 0x50
        0xC0, 0x03, 0x5F, 0xD6, // ret
    };
    static const uint8_t root_auth_repl[] = { NOP_BYTES, MOV_W0_0_BYTES };
    ck_patcher_find_replace(range, "bypass root authentication", root_auth,
                            NULL, sizeof(root_auth), sizeof(uint32_t),
                            root_auth_repl, NULL, 0, sizeof(root_auth_repl));

    static const uint8_t root_rw[] = {
        0x00, 0x00, 0x70, 0x37, // tbnz w0, 0xE, ?
        0xA0, 0x03, 0x40, 0xB9, // ldr x?, [x29/sp, ?]
        0x00, 0x78, 0x1F, 0x12, // and w?, w?, 0xFFFFFFFE
        0xA0, 0x03, 0x00, 0xB9, // str x?, [x29/sp, ?]
    };
    static const uint8_t root_rw_mask[] = {
        0x1F, 0x00, 0xF8, 0xFF, 0xA0, 0x03, 0xFE, 0xFF,
        0x00, 0xFC, 0xFF, 0xFF, 0xA0, 0x03, 0xC0, 0xFF,
    };
    QEMU_BUILD_BUG_ON(sizeof(root_rw) != sizeof(root_rw_mask));
    static const uint8_t root_rw_repl[] = { MOV_W0_0_BYTES };
    ck_patcher_find_replace(range, "allow mounting root as r/w", root_rw,
                            root_rw_mask, sizeof(root_rw), sizeof(uint32_t),
                            root_rw_repl, NULL, 0, sizeof(root_rw_repl));
}

static bool ck_kp_tc_callback(void *ctx, uint8_t *buffer)
{
    if (((ldl_le_p(buffer - 4) & 0xFF000000) != 0x91000000) &&
        ((ldl_le_p(buffer - 8) & 0xFF000000) != 0x91000000)) {
        return false;
    }

    void *ldrb =
        ck_patcher_find_next_insn(buffer, 256, 0x39402C00, 0xFFFFFC00, 0);
    uint32_t cdhash_param = extract32(ldl_le_p(ldrb), 5, 5);
    void *frame;
    void *start = buffer;
    bool pac;

    frame = ck_patcher_find_prev_insn(buffer, 10, 0x910003FD, 0xFF8003FF, 0);
    if (frame == NULL) {
        info_report("%s: found AMFI (Leaf)", __func__);
    } else {
        info_report("%s: found AMFI (Routine)", __func__);
        start = ck_patcher_find_prev_insn(frame, 10, 0xA9A003E0, 0xFFE003E0, 0);
        if (start == NULL) {
            start =
                ck_patcher_find_prev_insn(frame, 10, 0xD10003FF, 0xFF8003FF, 0);
            if (start == NULL) {
                error_report("%s: failed to find AMFI start", __func__);
                return false;
            }
        }
    }

    pac = ck_patcher_find_prev_insn(start, 5, PACIBSP, 0xFFFFFFFF, 0) != NULL;
    switch (cdhash_param) {
    case 0: {
        // adrp x8, ?
        void *adrp =
            ck_patcher_find_prev_insn(start, 10, 0x90000008, 0x9F00001F, 0);
        if (adrp != NULL) {
            start = adrp;
        }
        stl_le_p(start, 0x52802020); // mov w0, 0x101
        stl_le_p(start + 4, (pac ? RETAB : RET));
        return true;
    }
    case 1:
        stl_le_p(start, 0x52800040); // mov w0, 2
        stl_le_p(start + 4, 0x39000040); // strb w0, [x2]
        stl_le_p(start + 8, 0x52800020); // mov w0, 1
        stl_le_p(start + 12, 0x39000060); // strb w0, [x3]
        stl_le_p(start + 16, 0x52800020); // mov w0, 1
        stl_le_p(start + 20, (pac ? RETAB : RET));
        return true;
    default:
        error_report("%s: found unexpected AMFI prototype: %d", __func__,
                     cdhash_param);
        break;
    }
    return false;
}

static void ck_kp_tc_patch(CKPatcherRange *range)
{
    static const uint8_t pattern[] = {
        0x00, 0x02, 0x80, 0x52, // mov w?, 0x16
        0x00, 0x00, 0x00, 0xD3, // lsr ?
        0x00, 0x00, 0x00, 0x9B, // madd ?
    };
    static const uint8_t mask[] = { 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
                                    0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(pattern) != sizeof(mask));
    ck_patcher_find_callback(range, "AMFI, all binaries in trustcache", pattern,
                             mask, sizeof(pattern), sizeof(uint32_t),
                             ck_kp_tc_callback);
}

static bool ck_kp_tc_ios16_callback(void *ctx, uint8_t *buffer)
{
    void *start =
        ck_patcher_find_prev_insn(buffer, 100, PACIBSP, 0xFFFFFFFF, 0);

    if (start == NULL) {
        return false;
    }

    stl_le_p(start, 0x52802020); // mov w0, 0x101
    stl_le_p(start + 4, RET);

    return true;
}

static void ck_kp_tc_ios16_patch(CKPatcherRange *range)
{
    static const uint8_t pattern[] = { 0xC0, 0xCF, 0x9D,
                                       0xD2 }; // mov w?, 0xEE7E
    static const uint8_t mask[] = { 0xC0, 0xFF, 0xFF, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(pattern) != sizeof(mask));
    ck_patcher_find_callback(range, "AMFI, all binaries in trustcache (iOS 16)",
                             pattern, mask, sizeof(pattern), sizeof(uint32_t),
                             ck_kp_tc_ios16_callback);
}

static bool ck_kp_amfi_sha1(void *ctx, uint8_t *buffer)
{
    void *cmp = ck_patcher_find_next_insn(buffer, 0x10, 0x7100081F, 0xFFFFFFFF,
                                          0); // cmp w0, 2

    if (cmp == NULL) {
        error_report("%s: failed to find cmp", __func__);
        return false;
    }

    stl_le_p(cmp, 0x6B00001F); // cmp w0, w0
    return true;
}

static void ck_kp_amfi_patches(CKPatcherRange *range)
{
    static const uint8_t pattern[] = { 0x02, 0x00, 0xD0,
                                       0x36 }; // tbz w2, 0x1A, ?
    static const uint8_t mask[] = { 0x1F, 0x00, 0xF8, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(pattern) != sizeof(mask));
    ck_patcher_find_callback(range, "allow SHA1 signatures in AMFI", pattern,
                             mask, sizeof(pattern), sizeof(uint32_t),
                             ck_kp_amfi_sha1);
}

static bool ck_kp_mac_mount_callback(void *ctx, uint8_t *buffer)
{
    void *mac_mount =
        ck_patcher_find_prev_insn(buffer, 0x40, 0x37280000, 0xFFFE0000, 0);
    if (mac_mount == NULL) {
        mac_mount =
            ck_patcher_find_next_insn(buffer, 0x40, 0x37280000, 0xFFFE0000, 0);
        if (mac_mount == NULL) {
            error_report("%s: failed to find nop point", __func__);
            return false;
        }
    }

    // Allow MNT_UNION mounts
    stl_le_p(mac_mount, NOP);

    // Search for ldrb w8, [x?, 0x71]
    mac_mount =
        ck_patcher_find_prev_insn(buffer, 0x40, 0x3941C408, 0xFFFFFC1F, 0);
    if (mac_mount == NULL) {
        mac_mount =
            ck_patcher_find_next_insn(buffer, 0x40, 0x3941C408, 0xFFFFFC1F, 0);
        if (mac_mount == NULL) {
            error_report("%s: failed to find xzr point", __func__);
            return false;
        }
    }

    // Replace with a mov x8, xzr
    // This will bypass the (vp->v_mount->mnt_flag & MNT_ROOTFS) check
    stl_le_p(mac_mount, 0xAA1F03E8);

    return true;
}

static void ck_kp_mac_mount_patch(CKPatcherRange *range)
{
    static const uint8_t old[] = { 0xE9, 0x2F, 0x1F,
                                   0x32 }; // orr w9, wzr, 0x1FFE
    ck_patcher_find_callback(
        range, "allow remounting rootfs, union mounts (old)", old, NULL,
        sizeof(old), sizeof(uint32_t), ck_kp_mac_mount_callback);
    static const uint8_t new[] = { 0xC9, 0xFF, 0x83, 0x52 }; // movz w9, 0x1FFE
    ck_patcher_find_callback(
        range, "allow remounting rootfs, union mounts (new)", new, NULL,
        sizeof(new), sizeof(uint32_t), ck_kp_mac_mount_callback);
}

static void ck_kp_kprintf_patch(CKPatcherRange *range)
{
    static const uint8_t pattern[] = {
        0xAA, 0x43, 0x00, 0x91, // add x10, fp, #0x10
        0xEA, 0x07, 0x00, 0xF9, // str x10, [sp, #0x8]
        0x08, 0x00, 0x00, 0x2A, // orr w8, w?, w?
        0x08, 0x00, 0x00, 0x34, // cbz w8, #?
    };
    static const uint8_t mask[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0x1F, 0xFC, 0xE0, 0xFF,
                                    0x1F, 0x00, 0x00, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(pattern) != sizeof(mask));
    static const uint8_t replace[] = { 0xE8, 0x03, 0x1F, 0x2A };
    ck_patcher_find_replace(range, "force enable kprintf", pattern, mask,
                            sizeof(pattern), sizeof(uint32_t), replace, NULL, 8,
                            sizeof(replace));
}

static bool ck_kp_amx_callback(void *ctx, uint8_t *buffer)
{
    stl_le_p(buffer, 0x52810009); // mov w9, #0x800
    void *amx_ver_str =
        ck_patcher_find_prev_insn(buffer, 10, 0xB800000A, 0xFEC0001F, 1);
    if (amx_ver_str == NULL) {
        error_report("%s: failed to find gAMXVersion store.", __func__);
        return false;
    }
    stl_le_p(amx_ver_str, NOP);
    return true;
}

static void ck_kp_amx_patch(CKPatcherRange *range)
{
    static const uint8_t pattern[] = {
        0xE9, 0x83, 0x05, 0x32, // mov w9, #0x8000800
        0x09, 0x00, 0x00, 0xAA, // orr x9, x?, x?
    };
    static const uint8_t mask[] = { 0xFF, 0xFF, 0xFF, 0xFF,
                                    0x1F, 0xFC, 0xE0, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(pattern) != sizeof(mask));
    ck_patcher_find_callback(range, "disable AMX", pattern, mask,
                             sizeof(pattern), sizeof(uint32_t),
                             ck_kp_amx_callback);
}

static void ck_kp_apfs_snapshot_patch(CKPatcherRange *range)
{
    static const uint8_t pattern[] = "com.apple.os.update-";
    static const uint8_t repl[] = "shitcode.os.bullshit";
    QEMU_BUILD_BUG_ON(sizeof(pattern) != sizeof(repl));
    ck_patcher_find_replace(range, "disable APFS snapshots", pattern, NULL,
                            sizeof(pattern), 0, repl, NULL, 0, sizeof(repl));
}

// this will tell launchd this is an internal build,
// and that way we can get hactivation without bypassing
// or patching the activation procedure.
// This is NOT an iCloud bypass. This is utilising code that ALREADY exists
// in the activation daemon. This is essentially telling iOS, it's a
// development kernel/device, NOT the real product sold on market. IF you
// decide to use this knowledge to BYPASS technological countermeasures
// or any other intellectual theft or crime, YOU are responsible in full,
// AND SHOULD BE PROSECUTED TO THE FULL EXTENT OF THE LAW.
// We do NOT endorse nor approve the theft of property.
static void ck_kp_hactivation_patch(CKPatcherRange *range)
{
    static const uint8_t pattern[] = "\0release";
    static const uint8_t repl[] = "profile";
    ck_patcher_find_replace(range, "enable hactivation", pattern, NULL,
                            sizeof(pattern), 0, repl, NULL, 1, sizeof(repl));
}

static void ck_kp_sep_mgr_patches(CKPatcherRange *range)
{
    static const uint8_t pattern[] = {
        0x00, 0x04, 0x00, 0xF9, // str x?, [x?, #0x8]
        0x08, 0x04, 0x80, 0x52, // mov w8, #0x20
        0x08, 0x10, 0x00, 0xB9, // str w8, [x?, #0x10]
    };
    static const uint8_t mask[] = { 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0x1F, 0xFC, 0xFF, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(pattern) != sizeof(mask));
    static const uint8_t repl[] = { 0x28, 0x00, 0xA0,
                                    0x52 }; // mov w8, #0x10000
    ck_patcher_find_replace(
        range, "increase SCOT size to 0x10000 to use it as TRAC", pattern, mask,
        sizeof(pattern), sizeof(uint32_t), repl, NULL, 4, sizeof(repl));
}

static void ck_kp_img4_patches(CKPatcherRange *range)
{
    // caller of _Img4DecodePerformTrustEvaluationWithCallbacks
    static const uint8_t pattern[] = {
        0xE1, 0x03, 0x00, 0xAA, // mov x1, x?
        0x00, 0x00, 0x00, 0x94, // bl #?
        0x1F, 0x04, 0x00, 0x31, // cmn w0, #0x1
        0x00, 0x00, 0x00, 0x54, // b.eq #?
    };
    static const uint8_t mask[] = { 0xFF, 0xFF, 0xE0, 0xFF, 0x00, 0x00,
                                    0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0x1F, 0x00, 0xF8, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(pattern) != sizeof(mask));
    static const uint8_t repl[] = { MOV_W0_0_BYTES, NOP_BYTES }; // mov w0, #0
    ck_patcher_find_replace(
        range, "allow unsigned firmware in img4_firmware_evaluate", pattern,
        mask, sizeof(pattern), sizeof(uint32_t), repl, NULL, 8, sizeof(repl));

    static const uint8_t pattern_185[] = {
        0xE1, 0x03, 0x00, 0xAA, // mov x1, x?
        0xE4, 0x03, 0x00, 0xAA, // mov x4, x?
        0x00, 0x00, 0x00, 0x94, // bl #?
        0xF5, 0x03, 0x00, 0xAA, // mov x21, x0
        0x00, 0x00, 0x00, 0x34, // cbz #?
    };
    static const uint8_t mask_185[] = { 0xFF, 0xFF, 0xE0, 0xFF, 0xFF,
                                        0xFF, 0xE0, 0xFF, 0x00, 0x00,
                                        0x00, 0xFC, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0x1F, 0x00, 0xF8, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(pattern_185) != sizeof(mask_185));
    static const uint8_t repl_185[] = { MOV_W0_0_BYTES }; // mov w0, #0
    ck_patcher_find_replace(
        range, "allow unsigned firmware in img4_firmware_evaluate iOS 18.5",
        pattern_185, mask_185, sizeof(pattern_185), sizeof(uint32_t), repl_185,
        NULL, 8, sizeof(repl_185));
}

static void ck_kp_cs_patches(CKPatcherRange *range)
{
    // skip code signature checks in vm_fault_enter
    static const uint8_t pattern[] = {
        0x00, 0x00, 0x18, 0x36, // tbz w?, #3, #?
        0x00, 0x00, 0x80, 0x52, // mov w?, #0
    };
    static const uint8_t mask[] = { 0x00, 0x00, 0xF8, 0xFF,
                                    0xE0, 0xFF, 0xFF, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(pattern) != sizeof(mask));
    static const uint8_t repl[] = { NOP_BYTES };
    ck_patcher_find_replace(range, "bypass code signature checks", pattern,
                            mask, sizeof(pattern), sizeof(uint32_t), repl, NULL,
                            0, sizeof(repl));
    static const uint8_t alt[] = {
        0x00, 0x00, 0x18, 0x36, // tbz w?, #3, #?
        0x10, 0x02, 0x17, 0xAA, // mov x?, x?
        0x00, 0x00, 0x80, 0x52, // mov w?, #0
    };
    static const uint8_t mask_alt[] = { 0x00, 0x00, 0xF8, 0xFF, 0x10, 0xFE,
                                        0xFF, 0xFF, 0xE0, 0xFF, 0xFF, 0xFF };
    QEMU_BUILD_BUG_ON(sizeof(alt) != sizeof(mask_alt));
    ck_patcher_find_replace(range, "bypass code signature checks (alt)", alt,
                            mask_alt, sizeof(alt), sizeof(uint32_t), repl, NULL,
                            0, sizeof(repl));
}

static bool ck_kp_pmap_cs_enforce_callback(void *ctx, uint8_t *buffer)
{
    uint8_t *pacibsp =
        ck_patcher_find_prev_insn(buffer, 0x30, PACIBSP, 0xFFFFFFFF, 0);
    if (pacibsp == NULL) {
        error_report("%s: failed to find pacibsp", __func__);
        return false;
    }
    stl_le_p(pacibsp, MOV_W0_0);
    stl_le_p(pacibsp + 4, RET);
    return true;
}

static void ck_kp_pmap_cs_enforce_patch(CKPatcherRange *range)
{
    static const uint8_t pmap_cs_enforce[] = {
        0xE0, 0x03, 0x00, 0xAA, // mov x0, x?
        0xE1, 0x03, 0x00, 0xAA, // mov x1, x?
        0x02, 0x10, 0x80, 0x52, // mov w2, #0x80
        0x03, 0x10, 0x80, 0x52, // mov w3, #0x80
        0x04, 0x00, 0x80, 0x52, // mov w4, #0
        0x00, 0x00, 0x00, 0x94, // bl #?
    };
    static const uint8_t mask_pmap_cs_enforce[] = {
        0xFF, 0xFF, 0xE0, 0xFF, 0xFF, 0xFF, 0xE0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFC
    };
    QEMU_BUILD_BUG_ON(sizeof(pmap_cs_enforce) != sizeof(mask_pmap_cs_enforce));
    ck_patcher_find_callback(range, "bypass pmap_cs_enforce", pmap_cs_enforce,
                             mask_pmap_cs_enforce, sizeof(pmap_cs_enforce),
                             sizeof(uint32_t), ck_kp_pmap_cs_enforce_callback);
}

void ck_patch_kernel(MachoHeader64 *hdr)
{
    MachoHeader64 *apfs_hdr;
    g_autofree CKPatcherRange *apfs_text;
    g_autofree CKPatcherRange *apfs_cstring;
    g_autofree CKPatcherRange *amfi_text;
    g_autofree CKPatcherRange *sep_mgr_text;
    g_autofree CKPatcherRange *img4_text;
    g_autofree CKPatcherRange *kernel_text;
    g_autofree CKPatcherRange *kernel_const;
    g_autofree CKPatcherRange *kernel_ppltext;

    apfs_hdr = ck_kp_find_image_header(hdr, "com.apple.filesystems.apfs");
    apfs_text = ck_kp_find_section_range(apfs_hdr, "__TEXT_EXEC", "__text");
    ck_kp_apfs_patches(apfs_text);
    apfs_cstring = ck_kp_find_section_range(apfs_hdr, "__TEXT", "__cstring");
    if (apfs_cstring == NULL) {
        apfs_cstring = ck_kp_find_section_range(hdr, "__TEXT", "__cstring");
    }
    ck_kp_apfs_snapshot_patch(apfs_cstring);

    amfi_text =
        ck_kp_find_image_text(hdr, "com.apple.driver.AppleMobileFileIntegrity");
    ck_kp_amfi_patches(amfi_text);

    sep_mgr_text =
        ck_kp_find_image_text(hdr, "com.apple.driver.AppleSEPManager");
    ck_kp_sep_mgr_patches(sep_mgr_text);

    img4_text = ck_kp_find_image_text(hdr, "com.apple.security.AppleImage4");
    ck_kp_img4_patches(img4_text);

    kernel_text = ck_kp_get_kernel_section(hdr, "__TEXT_EXEC", "__text");
    ck_kp_tc_patch(kernel_text);
    ck_kp_mac_mount_patch(kernel_text);
    ck_kp_kprintf_patch(kernel_text);
    ck_kp_amx_patch(kernel_text);
    ck_kp_cs_patches(kernel_text);
    kernel_const = ck_kp_get_kernel_section(hdr, "__TEXT", "__const");
    ck_kp_hactivation_patch(kernel_const);

    kernel_ppltext = ck_kp_find_section_range(hdr, "__PPLTEXT", "__text");
    if (kernel_ppltext == NULL) {
        warn_report("Failed to find `__PPLTEXT.__text`.");
        ck_kp_pmap_cs_enforce_patch(kernel_text);
    } else {
        ck_kp_tc_patch(kernel_ppltext);
        ck_kp_tc_ios16_patch(kernel_ppltext);
        ck_kp_pmap_cs_enforce_patch(kernel_ppltext);
    }
}
