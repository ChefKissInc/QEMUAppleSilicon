/*
 * Apple t8030 SoC.
 *
 * Copyright (c) 2023-2024 Visual Ehrmanntraut (VisualEhrmanntraut).
 * Copyright (c) 2023-2024 Christian Inci (chris-pcguy).
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
#include "exec/address-spaces.h"
#include "exec/memattrs.h"
#include "exec/memory.h"
#include "hw/arm/apple-silicon/a13.h"
#include "hw/arm/apple-silicon/dart.h"
#include "hw/arm/apple-silicon/dtb.h"
#include "hw/arm/apple-silicon/mem.h"
#include "hw/arm/apple-silicon/sart.h"
#include "hw/arm/apple-silicon/sep-sim.h"
#include "hw/arm/apple-silicon/sep.h"
#include "hw/arm/apple-silicon/t8030-config.c.inc"
#include "hw/arm/apple-silicon/t8030.h"
#include "hw/block/apple_ans.h"
#include "hw/char/apple_uart.h"
#include "hw/display/apple_displaypipe_v2.h"
#include "hw/dma/apple_sio.h"
#include "hw/gpio/apple_gpio.h"
#include "hw/i2c/apple_i2c.h"
#include "hw/intc/apple_aic.h"
#include "hw/irq.h"
#include "hw/misc/apple-silicon/aes.h"
#include "hw/misc/apple-silicon/roswell.h"
#include "hw/misc/apple-silicon/smc.h"
#include "hw/misc/apple-silicon/spmi-pmu.h"
#include "hw/nvram/apple_nvram.h"
#include "hw/spmi/apple_spmi.h"
#include "hw/ssi/apple_spi.h"
#include "hw/ssi/ssi.h"
#include "hw/usb/apple_typec.h"
#include "hw/watchdog/apple_wdt.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/units.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "target/arm/arm-powerctl.h"

#define T8030_SROM_BASE 0x100000000ull
#define T8030_SROM_SIZE 0x80000ull

#define T8030_SRAM_BASE 0x19C000000ull
#define T8030_SRAM_SIZE 0x400000ull

#define T8030_DRAM_BASE 0x800000000ull
#define T8030_DRAM_SIZE (4ull * GiB)

#define T8030_SEPROM_BASE 0x240000000ull
#define T8030_SEPROM_SIZE 0x4000000ull

#define T8030_GPIO_FORCE_DFU 161

#define T8030_DISPLAY_BASE (T8030_DRAM_BASE + 0xF7FB4000)
#define T8030_DISPLAY_SIZE (67ull * MiB)

#define T8030_KERNEL_REGION_BASE T8030_DRAM_BASE
#define T8030_KERNEL_REGION_SIZE 0xF000000ull

#define T8030_SPI_BASE(_x) (0x35100000ull + (_x) * APPLE_SPI_MMIO_SIZE)

#define T8030_DWC2_IRQ 495

#define T8030_NUM_UARTS 9
#define T8030_NUM_SPIS 4

#define T8030_ANS_TEXT_BASE 0x800024000ull
#define T8030_ANS_TEXT_SIZE 0x124000ull
#define T8030_ANS_DATA_BASE 0x8FC400000ull
#define T8030_ANS_DATA_SIZE 0x3C00000ull
#define T8030_SMC_REGION_SIZE 0x80000ull
#define T8030_SMC_TEXT_BASE 0x23FE00000ull
#define T8030_SMC_TEXT_SIZE 0x30000ull
#define T8030_SMC_DATA_BASE 0x23FE30000ull
#define T8030_SMC_DATA_SIZE 0x30000ull
#define T8030_SMC_SRAM_BASE 0x23FE60000ull
#define T8030_SMC_SRAM_SIZE 0x4000ull

#define T8030_SIO_TEXT_BASE 0x8010A8000ull
#define T8030_SIO_TEXT_SIZE 0x1C000ull
#define T8030_SIO_TEXT_REMAP 0x200000ull
#define T8030_SIO_DATA_BASE 0x80186C000ull
#define T8030_SIO_DATA_SIZE 0xF8000ull
#define T8030_SIO_DATA_REMAP 0x220000ull

#define T8030_PANIC_BASE 0x8FC2B4000ull
#define T8030_PANIC_SIZE 0x100000ull

#define T8030_AMCC_BASE 0x200000000ull
#define T8030_AMCC_SIZE 0x100000ull
#define AMCC_PLANE_COUNT 4
#define AMCC_PLANE_STRIDE 0x40000ull
#define AMCC_LOWER(_p) (0x680 + (_p) * AMCC_PLANE_STRIDE)
#define AMCC_UPPER(_p) (0x684 + (_p) * AMCC_PLANE_STRIDE)
#define AMCC_REG(_tms, _x) *(uint32_t *)(&t8030_machine->amcc_reg[_x])

static size_t t8030_real_cpu_count(T8030MachineState *t8030_machine)
{
    MachineState *machine;

    machine = MACHINE(t8030_machine);

    return (t8030_machine->seprom_filename || t8030_machine->sep_fw_filename) ?
               machine->smp.cpus - 1 :
               machine->smp.cpus;
}

static void t8030_start_cpus(MachineState *machine, uint64_t cpu_mask)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    int i;

    for (i = 0; i < t8030_real_cpu_count(t8030_machine); i++) {
        if (test_bit(i, (unsigned long *)&cpu_mask) &&
            apple_a13_cpu_is_powered_off(t8030_machine->cpus[i])) {
            apple_a13_cpu_start(t8030_machine->cpus[i]);
        }
    }
}

static void t8030_create_s3c_uart(const T8030MachineState *t8030_machine,
                                  uint32_t port, Chardev *chr)
{
    DeviceState *dev;
    hwaddr base;
    // first fetch the uart mmio address
    int vector;
    DTBProp *prop;
    hwaddr *uart_offset;
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io/uart0");
    char name[32] = { 0 };

    g_assert(port < T8030_NUM_UARTS);

    g_assert(child);
    snprintf(name, sizeof(name), "uart%d", port);

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);

    uart_offset = (hwaddr *)prop->value;
    base = t8030_machine->soc_base_pa + uart_offset[0] + uart_offset[1] * port;

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);

    vector = *(uint32_t *)prop->value + port;
    dev = apple_uart_create(
        base, 15, 0, chr, qdev_get_gpio_in(DEVICE(t8030_machine->aic), vector));
    g_assert(dev);
    dev->id = g_strdup(name);
}

static void t8030_patch_kernel(MachoHeader64 *hdr)
{
}

static bool t8030_check_panic(MachineState *machine)
{
    T8030MachineState *t8030_machine;
    AppleEmbeddedPanicHeader *panic_info;
    bool ret;

    t8030_machine = T8030_MACHINE(machine);

    if (!t8030_machine->panic_size) {
        return false;
    }

    panic_info = g_malloc0(t8030_machine->panic_size);

    address_space_rw(&address_space_memory, t8030_machine->panic_base,
                     MEMTXATTRS_UNSPECIFIED, panic_info,
                     t8030_machine->panic_size, false);
    address_space_set(&address_space_memory, t8030_machine->panic_base, 0,
                      t8030_machine->panic_size, MEMTXATTRS_UNSPECIFIED);

    ret = panic_info->magic == EMBEDDED_PANIC_MAGIC;
    g_free(panic_info);
    return ret;
}

static size_t get_kaslr_random(void)
{
    size_t value = 0;
    qemu_guest_getrandom(&value, sizeof(value), NULL);
    return value;
}

#define L2_GRANULE ((0x4000) * (0x4000 / 8))
#define L2_GRANULE_MASK (L2_GRANULE - 1)

static void get_kaslr_slides(T8030MachineState *t8030_machine,
                             hwaddr *phys_slide_out, hwaddr *virt_slide_out)
{
    hwaddr slide_phys = 0, slide_virt = 0;
    const size_t slide_granular = (1 << 21);
    const size_t slide_granular_mask = slide_granular - 1;
    const size_t slide_virt_max = 0x100 * (2 * 1024 * 1024);
    size_t random_value = get_kaslr_random();

    if (t8030_machine->kaslr_off) {
        *phys_slide_out = 0;
        *virt_slide_out = 0;
        return;
    }

    slide_virt = (random_value & ~slide_granular_mask) % slide_virt_max;
    if (slide_virt == 0) {
        slide_virt = slide_virt_max;
    }
    slide_phys = slide_virt & L2_GRANULE_MASK;

    *phys_slide_out = slide_phys;
    *virt_slide_out = slide_virt;
}

static void t8030_load_classic_kc(T8030MachineState *t8030_machine,
                                  const char *cmdline)
{
    MachineState *machine = MACHINE(t8030_machine);
    MachoHeader64 *hdr = t8030_machine->kernel;
    MemoryRegion *sysmem = t8030_machine->sysmem;
    AddressSpace *nsas = &address_space_memory;
    hwaddr virt_low;
    hwaddr virt_end;
    hwaddr dtb_va;
    hwaddr top_of_kernel_data_pa;
    hwaddr mem_size;
    hwaddr phys_ptr;
    hwaddr amcc_lower;
    hwaddr amcc_upper;
    AppleBootInfo *info = &t8030_machine->bootinfo;
    hwaddr last_base;
    MachoSegmentCommand64 *last_seg;
    hwaddr text_base;
    DTBNode *memory_map =
        get_dtb_node(t8030_machine->device_tree, "/chosen/memory-map");

    g_phys_base = (hwaddr)macho_get_buffer(hdr);
    macho_highest_lowest(hdr, &virt_low, &virt_end);
    g_virt_base = virt_low;

    get_kaslr_slides(t8030_machine, &g_phys_slide, &g_virt_slide);

    last_seg = macho_get_segment(hdr, "__LAST");
    last_base = last_seg->vmaddr;
    text_base = macho_get_segment(hdr, "__TEXT")->vmaddr;

    g_phys_base = phys_ptr = T8030_KERNEL_REGION_BASE;
    phys_ptr += g_phys_slide;
    g_virt_base += g_virt_slide - g_phys_slide;

    // TrustCache
    info->trustcache_addr =
        vtop_static(text_base + g_virt_slide) - info->trustcache_size;

    macho_load_trustcache(t8030_machine->trustcache, info->trustcache_size,
                          nsas, sysmem, info->trustcache_addr);

    info->kern_entry = arm_load_macho(hdr, nsas, sysmem, memory_map,
                                      g_phys_base + g_phys_slide, g_virt_slide);
    info_report("Kernel virtual base: 0x" TARGET_FMT_lx, g_virt_base);
    info_report("Kernel physical base: 0x" TARGET_FMT_lx, g_phys_base);
    info_report("Kernel virtual slide: 0x" TARGET_FMT_lx, g_virt_slide);
    info_report("Kernel physical slide: 0x" TARGET_FMT_lx, g_phys_slide);
    info_report("Kernel entry point: 0x" TARGET_FMT_lx, info->kern_entry);

    virt_end += g_virt_slide;
    phys_ptr = vtop_static(align_16k_high(virt_end));

    amcc_lower = info->trustcache_addr;
    amcc_upper = vtop_static(last_base + g_virt_slide) + last_seg->vmsize - 1;
    for (int i = 0; i < 4; i++) {
        AMCC_REG(t8030_machine, AMCC_LOWER(i)) =
            (amcc_lower - T8030_DRAM_BASE) >> 14;
        AMCC_REG(t8030_machine, AMCC_UPPER(i)) =
            (amcc_upper - T8030_DRAM_BASE) >> 14;
    }

    // RAM disk
    if (machine->initrd_filename) {
        info->ramdisk_addr = phys_ptr;
        macho_load_ramdisk(machine->initrd_filename, nsas, sysmem,
                           info->ramdisk_addr, &info->ramdisk_size);
        info->ramdisk_size = align_16k_high(info->ramdisk_size);
        phys_ptr += info->ramdisk_size;
    }

    // SEPFW
    info->sep_fw_addr = phys_ptr;
    if (t8030_machine->sep_fw_filename) {
        macho_load_raw_file(t8030_machine->sep_fw_filename, nsas, sysmem,
                            "sepfw", info->sep_fw_addr, &info->sep_fw_size);
    }
    info->sep_fw_size = align_16k_high(8 * MiB);
    phys_ptr += info->sep_fw_size;

    // Kernel boot args
    info->kern_boot_args_addr = phys_ptr;
    info->kern_boot_args_size = 0x4000;
    phys_ptr += align_16k_high(0x4000);

    // Device tree
    info->device_tree_addr = phys_ptr;
    dtb_va = ptov_static(info->device_tree_addr);
    phys_ptr += align_16k_high(info->device_tree_size);

    mem_size =
        machine->maxram_size -
        (T8030_KERNEL_REGION_SIZE - (g_phys_base - T8030_KERNEL_REGION_BASE));

    macho_load_dtb(t8030_machine->device_tree, nsas, sysmem, "DeviceTree",
                   info);

    top_of_kernel_data_pa = (align_16k_high(phys_ptr) + 0x3000ull) & ~0x3fffull;

    info_report("Boot args: [%s]", cmdline);
    macho_setup_bootargs("BootArgs", nsas, sysmem, info->kern_boot_args_addr,
                         g_virt_base, g_phys_base, mem_size,
                         top_of_kernel_data_pa, dtb_va, info->device_tree_size,
                         t8030_machine->video_args, cmdline);
    g_virt_base = virt_low;
}

static void t8030_load_fileset_kc(T8030MachineState *t8030_machine,
                                  const char *cmdline)
{
    MachineState *machine = MACHINE(t8030_machine);
    MachoHeader64 *hdr = t8030_machine->kernel;
    MemoryRegion *sysmem = t8030_machine->sysmem;
    AddressSpace *nsas = &address_space_memory;
    hwaddr virt_low;
    hwaddr virt_end;
    hwaddr dtb_va;
    hwaddr top_of_kernel_data_pa;
    hwaddr mem_size;
    hwaddr phys_ptr;
    hwaddr amcc_lower;
    hwaddr amcc_upper;
    AppleBootInfo *info = &t8030_machine->bootinfo;
    uint64_t extradata_size;
    uint64_t l2_remaining;
    MachoSegmentCommand64 *prelink_info_seg;
    DTBNode *memory_map =
        get_dtb_node(t8030_machine->device_tree, "/chosen/memory-map");

    g_phys_base = (hwaddr)macho_get_buffer(hdr);
    macho_highest_lowest(hdr, &virt_low, &virt_end);
    g_virt_base = virt_low;

    prelink_info_seg = macho_get_segment(hdr, "__PRELINK_INFO");

    extradata_size =
        align_16k_high(info->device_tree_size + info->trustcache_size);
    g_assert(extradata_size < L2_GRANULE);

    get_kaslr_slides(t8030_machine, &g_phys_slide, &g_virt_slide);

    l2_remaining = (virt_low + g_virt_slide) & L2_GRANULE_MASK;

    if (extradata_size >= l2_remaining) {
        uint64_t grown_slide = align_16k_high(extradata_size - l2_remaining);
        g_phys_slide += grown_slide;
        g_virt_slide += grown_slide;
    }

    g_phys_base = phys_ptr = T8030_KERNEL_REGION_BASE;
    phys_ptr |= (virt_low & L2_GRANULE_MASK);
    phys_ptr += g_phys_slide;
    phys_ptr -= extradata_size;

    // Device tree
    info->device_tree_addr = phys_ptr;
    phys_ptr += info->device_tree_size;

    // TrustCache
    info->trustcache_addr = phys_ptr;
    macho_load_trustcache(t8030_machine->trustcache, info->trustcache_size,
                          nsas, sysmem, info->trustcache_addr);
    phys_ptr += align_16k_high(info->trustcache_size);

    g_virt_base += g_virt_slide;
    g_virt_base -= phys_ptr - g_phys_base;
    info->kern_entry =
        arm_load_macho(hdr, nsas, sysmem, memory_map, phys_ptr, g_virt_slide);
    info_report("Kernel virtual base: 0x" TARGET_FMT_lx, g_virt_base);
    info_report("Kernel physical base: 0x" TARGET_FMT_lx, g_phys_base);
    info_report("Kernel virtual slide: 0x" TARGET_FMT_lx, g_virt_slide);
    info_report("Kernel physical slide: 0x" TARGET_FMT_lx, g_phys_slide);
    info_report("Kernel entry point: 0x" TARGET_FMT_lx, info->kern_entry);

    virt_end += g_virt_slide;
    phys_ptr = vtop_static(align_16k_high(virt_end));

    amcc_lower = info->device_tree_addr;
    amcc_upper = vtop_static(prelink_info_seg->vmaddr + g_virt_slide) +
                 prelink_info_seg->vmsize - 1;
    for (int i = 0; i < 4; i++) {
        AMCC_REG(t8030_machine, AMCC_LOWER(i)) =
            (amcc_lower - T8030_DRAM_BASE) >> 14;
        AMCC_REG(t8030_machine, AMCC_UPPER(i)) =
            (amcc_upper - T8030_DRAM_BASE) >> 14;
    }

    dtb_va = ptov_static(info->device_tree_addr);

    if (machine->initrd_filename) {
        info->ramdisk_addr = phys_ptr;
        macho_load_ramdisk(machine->initrd_filename, nsas, sysmem,
                           info->ramdisk_addr, &info->ramdisk_size);
        info->ramdisk_size = align_16k_high(info->ramdisk_size);
        phys_ptr += info->ramdisk_size;
    }

    // SEPFW
    info->sep_fw_addr = phys_ptr;
    info->sep_fw_size = align_16k_high(8 * MiB);
    phys_ptr += info->sep_fw_size;

    info->kern_boot_args_addr = phys_ptr;
    info->kern_boot_args_size = 0x4000;
    phys_ptr += align_16k_high(0x4000);

    mem_size =
        machine->maxram_size -
        (T8030_KERNEL_REGION_SIZE - (g_phys_base - T8030_KERNEL_REGION_BASE));

    macho_load_dtb(t8030_machine->device_tree, nsas, sysmem, "DeviceTree",
                   info);

    top_of_kernel_data_pa = (align_16k_high(phys_ptr) + 0x3000ull) & ~0x3fffull;

    info_report("Boot args: [%s]", cmdline);
    macho_setup_bootargs("BootArgs", nsas, sysmem, info->kern_boot_args_addr,
                         g_virt_base, g_phys_base, mem_size,
                         top_of_kernel_data_pa, dtb_va, info->device_tree_size,
                         t8030_machine->video_args, cmdline);
    g_virt_base = virt_low;
}

static void t8030_memory_setup(MachineState *machine)
{
    MachoHeader64 *hdr;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    AppleNvramState *nvram = NULL;
    AppleBootInfo *info = &t8030_machine->bootinfo;
    DTBNode *memory_map =
        get_dtb_node(t8030_machine->device_tree, "/chosen/memory-map");
    AddressSpace *nsas = &address_space_memory;
    char *cmdline = NULL;
    char *seprom = NULL;
    unsigned long fsize = 0;

    if (t8030_check_panic(machine)) {
        qemu_system_guest_panicked(NULL);
        return;
    }

    info->dram_base = T8030_DRAM_BASE;
    info->dram_size = T8030_DRAM_SIZE;

    if (t8030_machine->seprom_filename) {
        if (!g_file_get_contents(t8030_machine->seprom_filename, &seprom,
                                 &fsize, NULL)) {
            error_setg(&error_fatal, "Could not load data from file '%s'",
                       t8030_machine->seprom_filename);
            return;
        }
        address_space_rw(nsas, T8030_SEPROM_BASE, MEMTXATTRS_UNSPECIFIED,
                         (uint8_t *)seprom, fsize, true);

        g_free(seprom);

        uint64_t value = 0x8000000000000000;
        address_space_write(nsas, t8030_machine->soc_base_pa + 0x42140108,
                            MEMTXATTRS_UNSPECIFIED, &value, sizeof(value));
        uint32_t value32 = 0x1;
        address_space_write(nsas, t8030_machine->soc_base_pa + 0x41448000,
                            MEMTXATTRS_UNSPECIFIED, &value32, sizeof(value32));
    }

    nvram = APPLE_NVRAM(qdev_find_recursive(sysbus_get_default(), "nvram"));
    if (!nvram) {
        error_setg(&error_abort, "%s: Failed to find nvram device", __func__);
        return;
    };
    apple_nvram_load(nvram);

    info_report("boot_mode: %u", t8030_machine->boot_mode);
    switch (t8030_machine->boot_mode) {
    case kBootModeEnterRecovery:
        env_set(nvram, "auto-boot", "false", 0);
        t8030_machine->boot_mode = kBootModeAuto;
        break;
    case kBootModeExitRecovery:
        env_set(nvram, "auto-boot", "true", 0);
        t8030_machine->boot_mode = kBootModeAuto;
        break;
    default:
        break;
    }

    info_report("auto-boot=%s",
                env_get_bool(nvram, "auto-boot", false) ? "true" : "false");

    switch (t8030_machine->boot_mode) {
    case kBootModeAuto:
        if (!env_get_bool(nvram, "auto-boot", false)) {
            asprintf(&cmdline, "-restore rd=md0 nand-enable-reformat=1 %s",
                     machine->kernel_cmdline);
            break;
        }
        QEMU_FALLTHROUGH;
    default:
        asprintf(&cmdline, "%s", machine->kernel_cmdline);
    }

    apple_nvram_save(nvram);

    info->nvram_size = nvram->len;

    if (info->nvram_size > XNU_MAX_NVRAM_SIZE) {
        info->nvram_size = XNU_MAX_NVRAM_SIZE;
    }
    if (apple_nvram_serialize(nvram, info->nvram_data,
                              sizeof(info->nvram_data)) < 0) {
        error_report("%s: Failed to read NVRAM", __func__);
    }

    if (t8030_machine->ticket_filename) {
        if (!g_file_get_contents(t8030_machine->ticket_filename,
                                 &info->ticket_data,
                                 (gsize *)&info->ticket_length, NULL)) {
            error_report("%s: Failed to read ticket from file %s", __func__,
                         t8030_machine->ticket_filename);
        }
    }

    if (xnu_contains_boot_arg(cmdline, "-restore", false)) {
        // HACK: Use DEV model to restore without FDR errors
        set_dtb_prop(t8030_machine->device_tree, "compatible", 28,
                     "N104DEV\0iPhone12,1\0AppleARM\0$");
    } else {
        set_dtb_prop(t8030_machine->device_tree, "compatible", 27,
                     "N104AP\0iPhone12,1\0AppleARM\0$");
    }

    if (!xnu_contains_boot_arg(cmdline, "rd=", true)) {
        DTBNode *chosen = find_dtb_node(t8030_machine->device_tree, "chosen");
        DTBProp *prop = find_dtb_prop(chosen, "root-matching");

        if (prop) {
            snprintf((char *)prop->value, prop->length,
                     "<dict><key>IOProviderClass</key><string>IOMedia</"
                     "string><key>IOPropertyMatch</key><dict><key>Partition "
                     "ID</key><integer>1</integer></dict></dict>");
        }
    }

    DTBNode *pram = find_dtb_node(t8030_machine->device_tree, "pram");
    if (pram) {
        uint64_t panic_reg[2] = { 0 };
        uint64_t panic_base = T8030_PANIC_BASE;
        uint64_t panic_size = T8030_PANIC_SIZE;

        panic_reg[0] = panic_base;
        panic_reg[1] = panic_size;

        set_dtb_prop(pram, "reg", sizeof(panic_reg), &panic_reg);
        DTBNode *chosen = find_dtb_node(t8030_machine->device_tree, "chosen");
        set_dtb_prop(chosen, "embedded-panic-log-size", 8, &panic_size);
        t8030_machine->panic_base = panic_base;
        t8030_machine->panic_size = panic_size;
    }

    DTBNode *vram = find_dtb_node(t8030_machine->device_tree, "vram");
    if (vram) {
        uint64_t vram_reg[2] = { 0 };
        uint64_t vram_base = T8030_DISPLAY_BASE;
        uint64_t vram_size = T8030_DISPLAY_SIZE;
        vram_reg[0] = vram_base;
        vram_reg[1] = vram_size;
        set_dtb_prop(vram, "reg", sizeof(vram_reg), &vram_reg);
    }

    hdr = t8030_machine->kernel;
    g_assert(hdr);

    macho_allocate_segment_records(memory_map, hdr);

    macho_populate_dtb(t8030_machine->device_tree, info);

    switch (hdr->file_type) {
    case MH_EXECUTE:
        t8030_load_classic_kc(t8030_machine, cmdline);
        break;
    case MH_FILESET:
        t8030_load_fileset_kc(t8030_machine, cmdline);
        break;
    default:
        error_setg(&error_abort, "%s: Unsupported kernelcache type: 0x%x\n",
                   __func__, hdr->file_type);
        break;
    }

    g_free(cmdline);
}

static void pmgr_unk_reg_write(void *opaque, hwaddr addr, uint64_t data,
                               unsigned size)
{
    hwaddr base = (hwaddr)opaque;
#if 0
    qemu_log_mask(LOG_UNIMP,
                  "PMGR reg WRITE unk @ 0x" TARGET_FMT_lx
                  " base: 0x" TARGET_FMT_lx " value: 0x" TARGET_FMT_lx "\n",
                  base + addr, base, data);
#endif
}

static uint64_t pmgr_unk_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    hwaddr base = (hwaddr)opaque;

#if 0
    qemu_log_mask(LOG_UNIMP,
                  "PMGR reg READ unk @ 0x" TARGET_FMT_lx
                  " base: 0x" TARGET_FMT_lx "\n",
                  base + addr, base);
#endif
    switch (base + addr) {
    case 0x3D280088: // PMGR_AON
        return 0xFF;
    case 0x3D2BC000:
        // return 0xA050C030; // IBFL | 0x00
        return 0xA55AC33C; // IBFL | 0x10
    case 0x3D2BC008:
        return 0xA55AC33C; // security domain | 0x1
    case 0x3D2BC00C:
        // return 0xA55AC33C; // security domain | 0x2
        return 0xA050C030; // security domain | 0x0
    case 0x3D2BC010:
        return (1 << 5) | (1 << 31); // _rCFG_FUSE0 ; (security epoch & 0x7F) <<
                                     // 5 ;; (1 << 31) for SEP
    case 0x3D2BC030:
        // return 0xFFFFFFFF; // CPRV
        // return 0x7 << 6; // LOW NIBBLE
        // return 0x70 << 5; // HIGH NIBBLE
        return 0x1 << 6;
    case 0x3D2BC300: // TODO
        return 0xCAFEBABE; // ECID lower
    case 0x3D2BC304: // TODO
        return 0xDEADBEEF; // ECID upper
    case 0x3D2BC400:
        // if 0xBC404 returns 1==0xA55AC33C, this will get ignored
        // return 0xA050C030; // CPFM | 0x00 ; IBFL_base == 0x04
        return 0xA55AC33C; // CPFM | 0x03 ; IBFL_base == 0x0C
    case 0x3D2BC404:
        // return 0xA55AC33C; // CPFM | 0x01 ; IBFL_base == 0x0C
        return 0xA050C030; // CPFM | 0x00 ; IBFL_base == 0x04
    case 0x3D2BC604: //?
        return 0xA050C030;
    case 0x3D2E8000: // ????
        return 0x32B3; // memory encryption AMK (Authentication Master Key)
                       // disabled
        // return 0xC2E9; // memory encryption AMK (Authentication Master Key)
        // enabled
    case 0x3D2D0034: //?
        return (1 << 24) | (1 << 25);
    default:
        if (((base + addr) & 0x10E70000) == 0x10E70000) {
            return (108 << 4) | 0x200000; //?
        }
        return 0;
    }
}

static const MemoryRegionOps pmgr_unk_reg_ops = {
    .write = pmgr_unk_reg_write,
    .read = pmgr_unk_reg_read,
};

static void pmgr_reg_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    MachineState *machine = MACHINE(opaque);
    T8030MachineState *t8030_machine = T8030_MACHINE(opaque);
    uint32_t value = data;

    if (addr >= 0x80000 && addr <= 0x8C000) {
        value = (value & 0xF) << 4 | (value & 0xF);
    }
#if 0
    qemu_log_mask(LOG_UNIMP,
                  "PMGR reg WRITE @ 0x" TARGET_FMT_lx " value: 0x" TARGET_FMT_lx
                  "\n",
                  addr, data);
#endif
    switch (addr) {
    case 0xD4004:
        t8030_start_cpus(machine, data);
        return;
    }
    memcpy(t8030_machine->pmgr_reg + addr, &value, size);
}

static uint64_t pmgr_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(opaque);
    uint64_t result = 0;
    switch (addr) {
    case 0xF0010: // AppleT8030PMGR::commonSramCheck
        result = 0x5000;
        break;
    case 0x80C00: // SEP Power State, Manual & Actual: Run Max
        result = 0xFF;
        break;
#if 0
    case 0xBC008:
        result = 0xFFFFFFFF;
        break;
    case 0xBC00C:
        result = 0xFFFFFFFF;
        break;
#endif
    default:
        memcpy(&result, t8030_machine->pmgr_reg + addr, size);
        break;
    }
#if 0
    qemu_log_mask(LOG_UNIMP,
                  "PMGR reg READ @ 0x" TARGET_FMT_lx " value: 0x" TARGET_FMT_lx
                  "\n",
                  addr, result);
#endif
    return result;
}

static const MemoryRegionOps pmgr_reg_ops = {
    .write = pmgr_reg_write,
    .read = pmgr_reg_read,
};

static void amcc_reg_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(opaque);
    uint32_t value = data;

    memcpy(t8030_machine->amcc_reg + addr, &value, size);
}

static uint64_t amcc_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(opaque);
    switch (addr) {
    case 0x6A0:
    case 0x406A0:
    case 0x806A0:
    case 0xC06A0:
        return 0x0;
    case 0x6A4:
        // return 0x1003;
    case 0x406A4:
        // return 0x2003;
    case 0x806A4:
        // return 0x3003;
    case 0xC06A4:
        // return 0x3;
        return 0x1003; // 0x1003 == 0x1004000
        // return 0x4003;
    case 0x6A8:
    case 0x406A8:
    case 0x806A8:
    case 0xC06A8:
        return 0x1;
    case 0x6B8:
    case 0x406B8:
    case 0x806B8:
    case 0xC06B8:
        return 0x1;
    default: {
        uint64_t result = 0;
        memcpy(&result, t8030_machine->amcc_reg + addr, size);
        return result;
    }
    }
}

static const MemoryRegionOps amcc_reg_ops = {
    .write = amcc_reg_write,
    .read = amcc_reg_read,
};

static void t8030_cluster_setup(MachineState *machine)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);

    for (int i = 0; i < A13_MAX_CLUSTER; i++) {
        char *name = NULL;

        name = g_strdup_printf("cluster%d", i);
        object_initialize_child(OBJECT(machine), name,
                                &t8030_machine->clusters[i],
                                TYPE_APPLE_A13_CLUSTER);
        g_free(name);
        qdev_prop_set_uint32(DEVICE(&t8030_machine->clusters[i]), "cluster-id",
                             i);
    }
}

static void t8030_cluster_realize(MachineState *machine)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    for (int i = 0; i < A13_MAX_CLUSTER; i++) {
        qdev_realize(DEVICE(&t8030_machine->clusters[i]), NULL, &error_fatal);
        if (t8030_machine->clusters[i].base) {
            memory_region_add_subregion(t8030_machine->sysmem,
                                        t8030_machine->clusters[i].base,
                                        &t8030_machine->clusters[i].mr);
        }
    }
}

static void t8030_cpu_setup(MachineState *machine)
{
    unsigned int i;
    DTBNode *root;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    GList *iter;
    GList *next = NULL;

    t8030_cluster_setup(machine);

    root = find_dtb_node(t8030_machine->device_tree, "cpus");
    g_assert(root);

    for (iter = root->child_nodes, i = 0; iter; iter = next, i++) {
        uint32_t cluster_id;
        DTBNode *node;

        next = iter->next;
        node = (DTBNode *)iter->data;
        if (i >= t8030_real_cpu_count(t8030_machine)) {
            remove_dtb_node(root, node);
            continue;
        }

        t8030_machine->cpus[i] = apple_a13_cpu_create(node, NULL, 0, 0, 0, 0);
        cluster_id = t8030_machine->cpus[i]->cluster_id;

        object_property_add_child(OBJECT(&t8030_machine->clusters[cluster_id]),
                                  DEVICE(t8030_machine->cpus[i])->id,
                                  OBJECT(t8030_machine->cpus[i]));
        qdev_realize(DEVICE(t8030_machine->cpus[i]), NULL, &error_fatal);
    }
    t8030_cluster_realize(machine);
}

static void t8030_create_aic(MachineState *machine)
{
    unsigned int i;
    hwaddr *reg;
    DTBProp *prop;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *soc = find_dtb_node(t8030_machine->device_tree, "arm-io");
    DTBNode *child;
    DTBNode *timebase;

    g_assert(soc);
    child = find_dtb_node(soc, "aic");
    g_assert(child);
    timebase = find_dtb_node(soc, "aic-timebase");
    g_assert(timebase);

    t8030_machine->aic =
        apple_aic_create(t8030_real_cpu_count(t8030_machine), child, timebase);
    object_property_add_child(OBJECT(machine), "aic",
                              OBJECT(t8030_machine->aic));
    g_assert(t8030_machine->aic);
    sysbus_realize(t8030_machine->aic, &error_fatal);

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);

    reg = (hwaddr *)prop->value;

    for (i = 0; i < t8030_real_cpu_count(t8030_machine); i++) {
        memory_region_add_subregion_overlap(
            &t8030_machine->cpus[i]->memory,
            t8030_machine->soc_base_pa + reg[0],
            sysbus_mmio_get_region(t8030_machine->aic, i), 0);
        sysbus_connect_irq(
            t8030_machine->aic, i,
            qdev_get_gpio_in(DEVICE(t8030_machine->cpus[i]), ARM_CPU_IRQ));
    }
}

static void t8030_pmgr_setup(MachineState *machine)
{
    uint64_t *reg;
    int i;
    char name[32];
    DTBProp *prop;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");

    g_assert(child);
    child = find_dtb_node(child, "pmgr");
    g_assert(child);

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);

    reg = (uint64_t *)prop->value;

    for (i = 0; i < prop->length / 8; i += 2) {
        MemoryRegion *mem = g_new(MemoryRegion, 1);
        if (i > 0) {
            snprintf(name, 32, "pmgr-unk-reg-%d", i);
            memory_region_init_io(mem, OBJECT(machine), &pmgr_unk_reg_ops,
                                  (void *)reg[i], name, reg[i + 1]);
        } else {
            memory_region_init_io(mem, OBJECT(machine), &pmgr_reg_ops,
                                  t8030_machine, "pmgr-reg", reg[i + 1]);
        }
        memory_region_add_subregion(t8030_machine->sysmem,
                                    reg[i] + reg[i + 1] <
                                            t8030_machine->soc_size ?
                                        t8030_machine->soc_base_pa + reg[i] :
                                        reg[i],
                                    mem);
    }

    {
        MemoryRegion *mem = g_new(MemoryRegion, 1);

        snprintf(name, 32, "pmp-reg");
        memory_region_init_io(mem, OBJECT(machine), &pmgr_unk_reg_ops,
                              (void *)0x3BC00000, name, 0x60000);
        memory_region_add_subregion(t8030_machine->sysmem,
                                    t8030_machine->soc_base_pa + 0x3BC00000,
                                    mem);
    }
    set_dtb_prop(child, "voltage-states5", sizeof(voltage_states5),
                 voltage_states5);
    set_dtb_prop(child, "voltage-states9-sram", sizeof(voltage_states9_sram),
                 voltage_states9_sram);
    set_dtb_prop(child, "voltage-states0", sizeof(voltage_states0),
                 voltage_states0);
    set_dtb_prop(child, "voltage-states9", sizeof(voltage_states9),
                 voltage_states9);
    set_dtb_prop(child, "voltage-states2", sizeof(voltage_states2),
                 voltage_states2);
    set_dtb_prop(child, "voltage-states1-sram", sizeof(voltage_states1_sram),
                 voltage_states1_sram);
    set_dtb_prop(child, "voltage-states10", sizeof(voltage_states10),
                 voltage_states10);
    set_dtb_prop(child, "voltage-states11", sizeof(voltage_states11),
                 voltage_states11);
    set_dtb_prop(child, "voltage-states8", sizeof(voltage_states8),
                 voltage_states8);
    set_dtb_prop(child, "voltage-states5-sram", sizeof(voltage_states5_sram),
                 voltage_states5_sram);
    set_dtb_prop(child, "voltage-states1", sizeof(voltage_states1),
                 voltage_states1);
    set_dtb_prop(child, "bridge-settings-17", sizeof(bridge_settings17),
                 bridge_settings17);
    set_dtb_prop(child, "bridge-settings-15", sizeof(bridge_settings15),
                 bridge_settings15);
    set_dtb_prop(child, "bridge-settings-13", sizeof(bridge_settings13),
                 bridge_settings13);
    set_dtb_prop(child, "bridge-settings-1", sizeof(bridge_settings1),
                 bridge_settings1);
    set_dtb_prop(child, "bridge-settings-5", sizeof(bridge_settings5),
                 bridge_settings5);
    set_dtb_prop(child, "bridge-settings-6", sizeof(bridge_settings6),
                 bridge_settings6);
    set_dtb_prop(child, "bridge-settings-2", sizeof(bridge_settings2),
                 bridge_settings2);
    set_dtb_prop(child, "bridge-settings-16", sizeof(bridge_settings16),
                 bridge_settings16);
    set_dtb_prop(child, "bridge-settings-14", sizeof(bridge_settings14),
                 bridge_settings14);
    set_dtb_prop(child, "bridge-settings-7", sizeof(bridge_settings7),
                 bridge_settings7);
    set_dtb_prop(child, "bridge-settings-12", sizeof(bridge_settings12),
                 bridge_settings12);
    set_dtb_prop(child, "bridge-settings-3", sizeof(bridge_settings3),
                 bridge_settings3);
    set_dtb_prop(child, "bridge-settings-8", sizeof(bridge_settings8),
                 bridge_settings8);
    set_dtb_prop(child, "bridge-settings-4", sizeof(bridge_settings4),
                 bridge_settings4);
    set_dtb_prop(child, "bridge-settings-0", sizeof(bridge_settings0),
                 bridge_settings0);
}

static void t8030_amcc_setup(MachineState *machine)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child;
    uint32_t data;
    uint64_t data64;

    child = get_dtb_node(t8030_machine->device_tree, "chosen");
    g_assert(child);
    child = get_dtb_node(child, "lock-regs");
    g_assert(child);
    child = get_dtb_node(child, "amcc");
    g_assert(child);
    data = 1;
    set_dtb_prop(child, "aperture-count", sizeof(data), &data);
    data = 0x100000;
    set_dtb_prop(child, "aperture-size", sizeof(data), &data);
    data = AMCC_PLANE_COUNT;
    set_dtb_prop(child, "plane-count", sizeof(data), &data);
    data = AMCC_PLANE_STRIDE;
    set_dtb_prop(child, "plane-stride", sizeof(data), &data);
    data64 = T8030_AMCC_BASE;
    set_dtb_prop(child, "aperture-phys-addr", sizeof(data64), &data64);
    data = 0x1c00;
    set_dtb_prop(child, "cache-status-reg-offset", sizeof(data), &data);
    data = 0x1f;
    set_dtb_prop(child, "cache-status-reg-mask", sizeof(data), &data);
    data = 0;
    set_dtb_prop(child, "cache-status-reg-value", sizeof(data), &data);
    child = get_dtb_node(child, "amcc-ctrr-a");

    data = 14;
    set_dtb_prop(child, "page-size-shift", sizeof(data), &data);

    data = AMCC_LOWER(0);
    set_dtb_prop(child, "lower-limit-reg-offset", sizeof(data), &data);
    data = 0xffffffff;
    set_dtb_prop(child, "lower-limit-reg-mask", sizeof(data), &data);
    data = AMCC_UPPER(0);
    set_dtb_prop(child, "upper-limit-reg-offset", sizeof(data), &data);
    data = 0xffffffff;
    set_dtb_prop(child, "upper-limit-reg-mask", sizeof(data), &data);
    data = 0x68c;
    set_dtb_prop(child, "lock-reg-offset", sizeof(data), &data);
    data = 1;
    set_dtb_prop(child, "lock-reg-mask", sizeof(data), &data);
    data = 1;
    set_dtb_prop(child, "lock-reg-value", sizeof(data), &data);

    memory_region_init_io(&t8030_machine->amcc, OBJECT(machine), &amcc_reg_ops,
                          t8030_machine, "amcc", T8030_AMCC_SIZE);
    memory_region_add_subregion(t8030_machine->sysmem, T8030_AMCC_BASE,
                                &t8030_machine->amcc);
}

static void t8030_create_dart(MachineState *machine, const char *name)
{
    AppleDARTState *dart = NULL;
    DTBProp *prop;
    uint64_t *reg;
    uint32_t *ints;
    int i;
    T8030MachineState *t8030_machine;
    DTBNode *child;

    t8030_machine = T8030_MACHINE(machine);
    child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    g_assert(child);

    child = find_dtb_node(child, name);
    if (!child)
        return;

    dart = apple_dart_create(child);
    g_assert(dart);
    object_property_add_child(OBJECT(machine), name, OBJECT(dart));

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);

    reg = (uint64_t *)prop->value;

    for (int i = 0; i < prop->length / 16; i++) {
        sysbus_mmio_map(SYS_BUS_DEVICE(dart), i,
                        t8030_machine->soc_base_pa + reg[i * 2]);
    }

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    ints = (uint32_t *)prop->value;

    for (i = 0; i < prop->length / sizeof(uint32_t); i++) {
        sysbus_connect_irq(
            SYS_BUS_DEVICE(dart), i,
            qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dart), &error_fatal);
}

static void t8030_create_sart(MachineState *machine)
{
    uint64_t *reg;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    DTBProp *prop;
    SysBusDevice *sart;

    g_assert(child);
    child = find_dtb_node(child, "sart-ans");
    g_assert(child);

    sart = apple_sart_create(child);
    g_assert(sart);
    object_property_add_child(OBJECT(machine), "sart-ans", OBJECT(sart));

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;

    sysbus_mmio_map(sart, 0, t8030_machine->soc_base_pa + reg[0]);
    sysbus_realize_and_unref(sart, &error_fatal);
}

static void t8030_create_ans(MachineState *machine)
{
    int i;
    uint32_t *ints;
    DTBProp *prop;
    uint64_t *reg;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    SysBusDevice *sart;
    SysBusDevice *ans;
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    DTBNode *iop_nub;
    AppleIopSegmentRange segranges[2] = { 0 };

    g_assert(child);
    child = find_dtb_node(child, "ans");
    g_assert(child);
    iop_nub = find_dtb_node(child, "iop-ans-nub");
    g_assert(iop_nub);

    prop = find_dtb_prop(iop_nub, "region-base");
    *(uint64_t *)prop->value = T8030_ANS_DATA_BASE;

    prop = find_dtb_prop(iop_nub, "region-size");
    *(uint64_t *)prop->value = T8030_ANS_DATA_SIZE;

    set_dtb_prop(iop_nub, "segment-names", 14, "__TEXT;__DATA");

    segranges[0].phys = T8030_ANS_TEXT_BASE;
    segranges[0].virt = 0x0;
    segranges[0].remap = T8030_ANS_TEXT_BASE;
    segranges[0].size = T8030_ANS_TEXT_SIZE;
    segranges[0].flag = 0x1;

    segranges[1].phys = T8030_ANS_DATA_BASE;
    segranges[1].virt = T8030_ANS_TEXT_SIZE;
    segranges[1].remap = T8030_ANS_DATA_BASE;
    segranges[1].size = T8030_ANS_DATA_SIZE;
    segranges[1].flag = 0x0;

    set_dtb_prop(iop_nub, "segment-ranges", sizeof(segranges), segranges);

    t8030_create_sart(machine);
    sart = SYS_BUS_DEVICE(
        object_property_get_link(OBJECT(machine), "sart-ans", &error_fatal));

    ans = apple_ans_create(child, APPLE_A7IOP_V4,
                           t8030_machine->rtbuddy_protocol_ver);
    g_assert(ans);
    g_assert(object_property_add_const_link(
        OBJECT(ans), "dma-mr", OBJECT(sysbus_mmio_get_region(sart, 1))));

    object_property_add_child(OBJECT(machine), "ans", OBJECT(ans));
    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;

    for (i = 0; i < 4; i++) {
        sysbus_mmio_map(ans, i, t8030_machine->soc_base_pa + reg[i << 1]);
    }

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    g_assert(prop->length == 20);
    ints = (uint32_t *)prop->value;

    for (i = 0; i < prop->length / sizeof(uint32_t); i++) {
        sysbus_connect_irq(
            ans, i, qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }

    sysbus_realize_and_unref(ans, &error_fatal);
}

static void t8030_create_gpio(MachineState *machine, const char *name)
{
    DeviceState *gpio = NULL;
    DTBProp *prop;
    uint64_t *reg;
    uint32_t *ints;
    int i;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");

    child = find_dtb_node(child, name);
    g_assert(child);
    gpio = apple_gpio_create(child);
    g_assert(gpio);
    object_property_add_child(OBJECT(machine), name, OBJECT(gpio));

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;
    sysbus_mmio_map(SYS_BUS_DEVICE(gpio), 0,
                    t8030_machine->soc_base_pa + reg[0]);
    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);

    ints = (uint32_t *)prop->value;

    for (i = 0; i < prop->length / sizeof(uint32_t); i++) {
        sysbus_connect_irq(
            SYS_BUS_DEVICE(gpio), i,
            qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(gpio), &error_fatal);
}

static void t8030_create_i2c(MachineState *machine, const char *name)
{
    SysBusDevice *i2c = NULL;
    DTBProp *prop;
    uint64_t *reg;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");

    child = find_dtb_node(child, name);
    if (!child)
        return;
    i2c = apple_i2c_create(name);
    g_assert(i2c);
    object_property_add_child(OBJECT(machine), name, OBJECT(i2c));

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;
    sysbus_mmio_map(i2c, 0, t8030_machine->soc_base_pa + reg[0]);
    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);

    sysbus_connect_irq(
        i2c, 0,
        qdev_get_gpio_in(DEVICE(t8030_machine->aic), *(uint32_t *)prop->value));

    sysbus_realize_and_unref(i2c, &error_fatal);
}

static void t8030_create_spi(MachineState *machine, uint32_t port)
{
    SysBusDevice *spi = NULL;
    DeviceState *gpio = NULL;
    DTBProp *prop;
    uint64_t *reg;
    uint32_t *ints;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    Object *sio;
    char name[32] = { 0 };
    hwaddr base = t8030_machine->soc_base_pa + T8030_SPI_BASE(port);
    uint32_t irq = spi_irqs[port];
    uint32_t cs_pin = spi_cs_pins[port];

    g_assert(port < T8030_NUM_SPIS);
    snprintf(name, sizeof(name), "spi%d", port);
    child = find_dtb_node(child, name);

    if (child) {
        spi = apple_spi_create(child);
    } else {
        spi = SYS_BUS_DEVICE(qdev_new(TYPE_APPLE_SPI));
        DEVICE(spi)->id = g_strdup(name);
    }
    g_assert(spi);
    object_property_add_child(OBJECT(machine), name, OBJECT(spi));

    sio = object_property_get_link(OBJECT(machine), "sio", &error_fatal);
    g_assert(object_property_add_const_link(OBJECT(spi), "sio", sio));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(spi), &error_fatal);

    if (child) {
        prop = find_dtb_prop(child, "reg");
        g_assert(prop);
        reg = (uint64_t *)prop->value;
        base = t8030_machine->soc_base_pa + reg[0];

        prop = find_dtb_prop(child, "interrupts");
        g_assert(prop);
        ints = (uint32_t *)prop->value;
        irq = ints[0];
    }
    sysbus_mmio_map(spi, 0, base);

    // The second sysbus IRQ is the cs line
    sysbus_connect_irq(SYS_BUS_DEVICE(spi), 0,
                       qdev_get_gpio_in(DEVICE(t8030_machine->aic), irq));

    if (child) {
        prop = find_dtb_prop(child, "function-spi_cs0");
        if (prop) {
            ints = (uint32_t *)prop->value;
            cs_pin = ints[2];
        }
    }
    if (cs_pin != -1) {
        gpio = DEVICE(
            object_property_get_link(OBJECT(machine), "gpio", &error_fatal));
        g_assert(gpio);
        qdev_connect_gpio_out(
            gpio, cs_pin, qdev_get_gpio_in_named(DEVICE(spi), SSI_GPIO_CS, 0));
    }
}

static void t8030_create_usb(MachineState *machine)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    DTBNode *drd = find_dtb_node(child, "usb-drd");
    DTBNode *dart_usb = find_dtb_node(child, "dart-usb");
    DTBNode *dart_mapper = find_dtb_node(dart_usb, "mapper-usb-drd");
    DTBNode *dart_dwc2_mapper = find_dtb_node(dart_usb, "mapper-usb-device");
    DTBNode *phy = find_dtb_node(child, "atc-phy");
    DTBProp *prop;
    DeviceState *atc;
    AppleDARTState *dart;
    IOMMUMemoryRegion *iommu = NULL;
    uint32_t *ints;

    set_dtb_prop(drd, "device-mac-address", 6, "\xBC\xDE\x48\x33\x44\x55");
    set_dtb_prop(drd, "host-mac-address", 6, "\xBC\xDE\x48\x00\x11\x22");

    dart = APPLE_DART(
        object_property_get_link(OBJECT(machine), "dart-usb", &error_fatal));

    atc = qdev_new(TYPE_APPLE_TYPEC);
    object_property_add_child(OBJECT(machine), "atc", OBJECT(atc));

    prop = find_dtb_prop(dart_mapper, "reg");
    g_assert(prop);
    g_assert(prop->length == 4);
    iommu = apple_dart_instance_iommu_mr(dart, 1, *(uint32_t *)prop->value);
    g_assert(iommu);

    g_assert(
        object_property_add_const_link(OBJECT(atc), "dma-xhci", OBJECT(iommu)));
    g_assert(
        object_property_add_const_link(OBJECT(atc), "dma-drd", OBJECT(iommu)));

    prop = find_dtb_prop(dart_dwc2_mapper, "reg");
    g_assert(prop);
    g_assert(prop->length == 4);
    iommu = apple_dart_instance_iommu_mr(dart, 1, *(uint32_t *)prop->value);
    g_assert(iommu);

    g_assert(
        object_property_add_const_link(OBJECT(atc), "dma-otg", OBJECT(iommu)));

    prop = find_dtb_prop(phy, "reg");
    g_assert(prop);
    sysbus_mmio_map(SYS_BUS_DEVICE(atc), 0,
                    t8030_machine->soc_base_pa + ((uint64_t *)prop->value)[0]);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(atc), &error_fatal);

    prop = find_dtb_prop(drd, "interrupts");
    g_assert(prop);
    ints = (uint32_t *)prop->value;
    for (int i = 0; i < 4; i++) {
        sysbus_connect_irq(
            SYS_BUS_DEVICE(atc), i,
            qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }
    sysbus_connect_irq(
        SYS_BUS_DEVICE(atc), 4,
        qdev_get_gpio_in(DEVICE(t8030_machine->aic), T8030_DWC2_IRQ));
}

static void t8030_create_wdt(MachineState *machine)
{
    int i;
    uint32_t *ints;
    DTBProp *prop;
    uint64_t *reg;
    uint32_t value;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    SysBusDevice *wdt;
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");

    g_assert(child);
    child = find_dtb_node(child, "wdt");
    g_assert(child);

    wdt = apple_wdt_create(child);
    g_assert(wdt);

    object_property_add_child(OBJECT(machine), "wdt", OBJECT(wdt));
    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;

    sysbus_mmio_map(wdt, 0, t8030_machine->soc_base_pa + reg[0]);
    sysbus_mmio_map(wdt, 1, t8030_machine->soc_base_pa + reg[2]);

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    ints = (uint32_t *)prop->value;

    for (i = 0; i < prop->length / sizeof(uint32_t); i++) {
        sysbus_connect_irq(
            wdt, i, qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }

    // TODO: MCC
    prop = find_dtb_prop(child, "function-panic_flush_helper");
    if (prop) {
        remove_dtb_prop(child, prop);
    }

    prop = find_dtb_prop(child, "function-panic_halt_helper");
    if (prop) {
        remove_dtb_prop(child, prop);
    }

    value = 1;
    set_dtb_prop(child, "no-pmu", sizeof(value), &value);

    sysbus_realize_and_unref(wdt, &error_fatal);
}

static void t8030_create_aes(MachineState *machine)
{
    uint32_t *ints;
    DTBProp *prop;
    uint64_t *reg;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    SysBusDevice *aes;
    AppleDARTState *dart;
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    IOMMUMemoryRegion *dma_mr = NULL;
    DTBNode *dart_sio = find_dtb_node(child, "dart-sio");
    DTBNode *dart_aes_mapper = find_dtb_node(dart_sio, "mapper-aes");

    g_assert(child);
    child = find_dtb_node(child, "aes");
    g_assert(child);
    g_assert(dart_sio);
    g_assert(dart_aes_mapper);

    aes = apple_aes_create(child);
    g_assert(aes);

    object_property_add_child(OBJECT(machine), "aes", OBJECT(aes));
    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;

    sysbus_mmio_map(aes, 0, t8030_machine->soc_base_pa + reg[0]);
    sysbus_mmio_map(aes, 1, t8030_machine->soc_base_pa + reg[2]);

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    g_assert(prop->length == 4);
    ints = (uint32_t *)prop->value;

    sysbus_connect_irq(aes, 0,
                       qdev_get_gpio_in(DEVICE(t8030_machine->aic), *ints));

    dart = APPLE_DART(
        object_property_get_link(OBJECT(machine), "dart-sio", &error_fatal));
    g_assert(dart);

    prop = find_dtb_prop(dart_aes_mapper, "reg");

    dma_mr = apple_dart_iommu_mr(dart, *(uint32_t *)prop->value);
    g_assert(dma_mr);
    g_assert(
        object_property_add_const_link(OBJECT(aes), "dma-mr", OBJECT(dma_mr)));

    sysbus_realize_and_unref(aes, &error_fatal);
}

static void t8030_create_spmi(MachineState *machine, const char *name)
{
    SysBusDevice *spmi = NULL;
    DTBProp *prop;
    uint64_t *reg;
    uint32_t *ints;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");

    g_assert(child);
    child = find_dtb_node(child, name);
    if (!child)
        return;

    spmi = apple_spmi_create(child);
    g_assert(spmi);
    object_property_add_child(OBJECT(machine), name, OBJECT(spmi));

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);

    reg = (uint64_t *)prop->value;

    sysbus_mmio_map(SYS_BUS_DEVICE(spmi), 0,
                    (t8030_machine->soc_base_pa + reg[2]) &
                        ~(APPLE_SPMI_MMIO_SIZE - 1));

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    ints = (uint32_t *)prop->value;
    // XXX: Only the second interrupt's parent is AIC
    sysbus_connect_irq(SYS_BUS_DEVICE(spmi), 0,
                       qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[1]));

    sysbus_realize_and_unref(SYS_BUS_DEVICE(spmi), &error_fatal);
}

static void t8030_create_pmu(MachineState *machine, const char *parent,
                             const char *name)
{
    DeviceState *pmu = NULL;
    AppleSPMIState *spmi = NULL;
    DTBProp *prop;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    uint32_t *ints;

    g_assert(child);
    child = find_dtb_node(child, parent);
    if (!child)
        return;

    spmi = APPLE_SPMI(
        object_property_get_link(OBJECT(machine), parent, &error_fatal));
    g_assert(spmi);

    child = find_dtb_node(child, name);
    if (!child)
        return;

    pmu = apple_spmi_pmu_create(child);
    g_assert(pmu);
    object_property_add_child(OBJECT(machine), name, OBJECT(pmu));

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    ints = (uint32_t *)prop->value;

    qdev_connect_gpio_out(pmu, 0, qdev_get_gpio_in(DEVICE(spmi), ints[0]));
    spmi_slave_realize_and_unref(SPMI_SLAVE(pmu), spmi->bus, &error_fatal);
}

static void t8030_create_smc(MachineState *machine)
{
    int i;
    uint32_t *ints;
    DTBProp *prop;
    uint64_t *reg;
    uint64_t data;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    SysBusDevice *smc;
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    DTBNode *iop_nub;
    AppleIopSegmentRange segranges[2] = { 0 };

    g_assert(child);
    child = find_dtb_node(child, "smc");
    g_assert(child);
    iop_nub = find_dtb_node(child, "iop-smc-nub");
    g_assert(iop_nub);

    set_dtb_prop(iop_nub, "segment-names", 14, "__TEXT;__DATA");

    segranges[0].phys = T8030_SMC_TEXT_BASE;
    segranges[0].virt = 0x0;
    segranges[0].remap = T8030_SMC_TEXT_BASE;
    segranges[0].size = T8030_SMC_TEXT_SIZE;
    segranges[0].flag = 0x1;

    segranges[1].phys = T8030_SMC_DATA_BASE;
    segranges[1].virt = T8030_SMC_TEXT_SIZE;
    segranges[1].remap = T8030_SMC_DATA_BASE;
    segranges[1].size = T8030_SMC_DATA_SIZE;
    segranges[1].flag = 0x0;

    set_dtb_prop(iop_nub, "segment-ranges", sizeof(segranges), segranges);

    data = T8030_SMC_REGION_SIZE;
    set_dtb_prop(iop_nub, "region-size", sizeof(data), &data);
    data = T8030_SMC_SRAM_BASE;
    set_dtb_prop(iop_nub, "sram-addr", sizeof(data), &data);

    smc = apple_smc_create(child, APPLE_A7IOP_V4,
                           t8030_machine->rtbuddy_protocol_ver);
    g_assert(smc);

    object_property_add_child(OBJECT(machine), "smc", OBJECT(smc));
    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;

    for (int i = 0; i < prop->length / 16; i++) {
        sysbus_mmio_map(smc, i, t8030_machine->soc_base_pa + reg[i * 2]);
    }

    sysbus_mmio_map(smc, 2, T8030_SMC_SRAM_BASE);

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    ints = (uint32_t *)prop->value;

    for (i = 0; i < prop->length / sizeof(uint32_t); i++) {
        sysbus_connect_irq(
            smc, i, qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }

    sysbus_realize_and_unref(smc, &error_fatal);
}

static void t8030_create_sio(MachineState *machine)
{
    int i;
    uint32_t *ints;
    DTBProp *prop;
    uint64_t *reg;
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    SysBusDevice *sio;
    AppleDARTState *dart;
    DTBNode *child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    DTBNode *iop_nub;
    AppleIopSegmentRange segranges[2] = { 0 };
    IOMMUMemoryRegion *dma_mr = NULL;
    DTBNode *dart_sio = find_dtb_node(child, "dart-sio");
    DTBNode *dart_sio_mapper = find_dtb_node(dart_sio, "mapper-sio");

    g_assert(child);
    child = find_dtb_node(child, "sio");
    g_assert(child);
    iop_nub = find_dtb_node(child, "iop-sio-nub");
    g_assert(iop_nub);

    set_dtb_prop(child, "segment-names", 14, "__TEXT;__DATA");
    set_dtb_prop(iop_nub, "segment-names", 14, "__TEXT;__DATA");

    segranges[0].phys = T8030_SIO_TEXT_BASE;
    segranges[0].virt = 0x0;
    segranges[0].remap = T8030_SIO_TEXT_REMAP;
    segranges[0].size = T8030_SIO_TEXT_SIZE;
    segranges[0].flag = 0x1;

    segranges[1].phys = T8030_SIO_DATA_BASE;
    segranges[1].virt = T8030_SIO_TEXT_SIZE;
    segranges[1].remap = T8030_SIO_DATA_REMAP;
    segranges[1].size = T8030_SIO_DATA_SIZE;
    segranges[1].flag = 0x0;

    set_dtb_prop(child, "segment-ranges", sizeof(segranges), segranges);
    set_dtb_prop(iop_nub, "segment-ranges", sizeof(segranges), segranges);

    sio = apple_sio_create(child, APPLE_A7IOP_V4,
                           t8030_machine->rtbuddy_protocol_ver);
    g_assert(sio);

    object_property_add_child(OBJECT(machine), "sio", OBJECT(sio));
    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;

    for (int i = 0; i < 2; i++) {
        sysbus_mmio_map(sio, i, t8030_machine->soc_base_pa + reg[i * 2]);
    }

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    ints = (uint32_t *)prop->value;

    for (i = 0; i < prop->length / sizeof(uint32_t); i++) {
        sysbus_connect_irq(
            sio, i, qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }

    dart = APPLE_DART(
        object_property_get_link(OBJECT(machine), "dart-sio", &error_fatal));
    g_assert(dart);

    prop = find_dtb_prop(dart_sio_mapper, "reg");

    dma_mr = apple_dart_iommu_mr(dart, *(uint32_t *)prop->value);
    g_assert(dma_mr);
    g_assert(
        object_property_add_const_link(OBJECT(sio), "dma-mr", OBJECT(dma_mr)));

    sysbus_realize_and_unref(sio, &error_fatal);
}

static void t8030_roswell_create(MachineState *machine)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *child;
    DTBProp *prop;

    child = find_dtb_node(t8030_machine->device_tree, "arm-io/i2c3/roswell");
    g_assert(child);

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    apple_roswell_create(machine, *(uint32_t *)prop->value);
}

static void t8030_display_create(MachineState *machine)
{
    T8030MachineState *t8030_machine;
    AppleDisplayPipeV2State *s;
    SysBusDevice *sbd;
    DTBNode *child;
    uint64_t *reg;
    DTBProp *prop;

    t8030_machine = T8030_MACHINE(machine);

    child = find_dtb_node(t8030_machine->device_tree, "arm-io/disp0");

    s = apple_displaypipe_v2_create(machine, child);
    sbd = SYS_BUS_DEVICE(s);
    t8030_machine->video_args.base_addr = T8030_DISPLAY_BASE;
    t8030_machine->video_args.row_bytes = s->width * 4;
    t8030_machine->video_args.width = s->width;
    t8030_machine->video_args.height = s->height;
    t8030_machine->video_args.depth = 32 | ((2 - 1) << 16);
    t8030_machine->video_args.display = 1;
    if (xnu_contains_boot_arg(machine->kernel_cmdline, "-s", false) ||
        xnu_contains_boot_arg(machine->kernel_cmdline, "-v", false)) {
        t8030_machine->video_args.display = 0;
    }

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;

    sysbus_mmio_map(sbd, 0, t8030_machine->soc_base_pa + reg[0]);

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    uint32_t *ints = (uint32_t *)prop->value;

    for (size_t i = 0; i < prop->length / sizeof(uint32_t); i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
        sysbus_connect_irq(
            sbd, i, qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }

    AppleDARTState *dart = APPLE_DART(
        object_property_get_link(OBJECT(machine), "dart-disp0", &error_fatal));
    g_assert(dart);
    child = find_dtb_node(t8030_machine->device_tree,
                          "arm-io/dart-disp0/mapper-disp0");
    g_assert(child);
    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    s->dma_mr =
        MEMORY_REGION(apple_dart_iommu_mr(dart, *(uint32_t *)prop->value));
    g_assert(s->dma_mr);
    g_assert(object_property_add_const_link(OBJECT(sbd), "dma_mr",
                                            OBJECT(s->dma_mr)));
    address_space_init(&s->dma_as, s->dma_mr, "disp0.dma");

    memory_region_init_ram(&s->vram, OBJECT(sbd), "vram", T8030_DISPLAY_SIZE,
                           &error_fatal);
    memory_region_add_subregion_overlap(t8030_machine->sysmem,
                                        t8030_machine->video_args.base_addr,
                                        &s->vram, 1);
    object_property_add_const_link(OBJECT(sbd), "vram", OBJECT(&s->vram));
    object_property_add_child(OBJECT(machine), "disp0", OBJECT(sbd));

    sysbus_realize_and_unref(sbd, &error_fatal);
}

static void t8030_create_sep(MachineState *machine)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *armio;
    DTBNode *child;
    AppleSEPState *sep;
    DTBProp *prop;
    uint64_t *reg;
    uint32_t *ints;
    AppleDARTState *dart;

    armio = find_dtb_node(t8030_machine->device_tree, "arm-io");
    g_assert(armio);
    child = find_dtb_node(armio, "sep");
    g_assert(child);

    sep = apple_sep_create(child, T8030_SEPROM_BASE, A13_MAX_CPU + 1,
                           t8030_machine->build_version, true);
    g_assert(sep);

    object_property_add_child(OBJECT(machine), "sep", OBJECT(sep));

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;
    sysbus_mmio_map(SYS_BUS_DEVICE(sep), 0,
                    t8030_machine->soc_base_pa + reg[0]);
    sysbus_mmio_map(SYS_BUS_DEVICE(sep), 1,
                    t8030_machine->soc_base_pa + 0x41100000); // TRNG
    sysbus_mmio_map(SYS_BUS_DEVICE(sep), 2,
                    t8030_machine->soc_base_pa + 0x41080000); // MISC0
    sysbus_mmio_map(SYS_BUS_DEVICE(sep), 3,
                    t8030_machine->soc_base_pa + 0x41040000); // MISC1
    sysbus_mmio_map(SYS_BUS_DEVICE(sep), 4,
                    t8030_machine->soc_base_pa + 0x410C4000); // MISC2

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    ints = (uint32_t *)prop->value;

    for (int i = 0; i < prop->length / sizeof(uint32_t); i++) {
        sysbus_connect_irq(
            SYS_BUS_DEVICE(sep), i,
            qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }

    dart = APPLE_DART(
        object_property_get_link(OBJECT(machine), "dart-sep", &error_fatal));
    g_assert(dart);
    child = find_dtb_node(armio, "dart-sep");
    g_assert(child);
    child = find_dtb_node(child, "mapper-sep");
    g_assert(child);
    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    sep->dma_mr =
        MEMORY_REGION(apple_dart_iommu_mr(dart, *(uint32_t *)prop->value));
    g_assert(sep->dma_mr);
    g_assert(object_property_add_const_link(OBJECT(sep), "dma-mr",
                                            OBJECT(sep->dma_mr)));
    sep->dma_as = g_new0(AddressSpace, 1);
    address_space_init(sep->dma_as, sep->dma_mr, "sep.dma");

    sysbus_realize_and_unref(SYS_BUS_DEVICE(sep), &error_fatal);
}

static void t8030_create_sep_sim(MachineState *machine)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DTBNode *armio;
    DTBNode *child;
    AppleSEPSimState *sep;
    DTBProp *prop;
    uint64_t *reg;
    uint32_t *ints;
    AppleDARTState *dart;

    armio = find_dtb_node(t8030_machine->device_tree, "arm-io");
    g_assert(armio);
    child = find_dtb_node(armio, "sep");
    g_assert(child);

    sep = apple_sep_sim_create(child, true);
    g_assert(sep);

    object_property_add_child(OBJECT(machine), "sep", OBJECT(sep));

    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    reg = (uint64_t *)prop->value;
    sysbus_mmio_map(SYS_BUS_DEVICE(sep), 0,
                    t8030_machine->soc_base_pa + reg[0]);

    prop = find_dtb_prop(child, "interrupts");
    g_assert(prop);
    ints = (uint32_t *)prop->value;

    for (int i = 0; i < prop->length / sizeof(uint32_t); i++) {
        sysbus_connect_irq(
            SYS_BUS_DEVICE(sep), i,
            qdev_get_gpio_in(DEVICE(t8030_machine->aic), ints[i]));
    }

    dart = APPLE_DART(
        object_property_get_link(OBJECT(machine), "dart-sep", &error_fatal));
    g_assert(dart);
    child = find_dtb_node(armio, "dart-sep");
    g_assert(child);
    child = find_dtb_node(child, "mapper-sep");
    g_assert(child);
    prop = find_dtb_prop(child, "reg");
    g_assert(prop);
    sep->dma_mr =
        MEMORY_REGION(apple_dart_iommu_mr(dart, *(uint32_t *)prop->value));
    g_assert(sep->dma_mr);
    g_assert(object_property_add_const_link(OBJECT(sep), "dma-mr",
                                            OBJECT(sep->dma_mr)));
    sep->dma_as = g_new0(AddressSpace, 1);
    address_space_init(sep->dma_as, sep->dma_mr, "sep.dma");

    sysbus_realize_and_unref(SYS_BUS_DEVICE(sep), &error_fatal);
}

static void t8030_cpu_reset_work(CPUState *cpu, run_on_cpu_data data)
{
    T8030MachineState *t8030_machine;

    t8030_machine = data.host_ptr;
    cpu_reset(cpu);
    ARM_CPU(cpu)->env.xregs[0] = t8030_machine->bootinfo.kern_boot_args_addr;
    cpu_set_pc(cpu, t8030_machine->bootinfo.kern_entry);
}

static void t8030_cpu_reset(void *opaque)
{
    MachineState *machine = MACHINE(opaque);
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    CPUState *cpu;
    uint64_t m_lo;
    uint64_t m_hi;

    qemu_guest_getrandom(&m_lo, sizeof(m_lo), NULL);
    qemu_guest_getrandom(&m_hi, sizeof(m_hi), NULL);

    CPU_FOREACH (cpu) {
        AppleA13State *tcpu = APPLE_A13(cpu);
        if (tcpu == NULL) {
            continue;
        }
        object_property_set_uint(OBJECT(cpu), "rvbar",
                                 t8030_machine->bootinfo.kern_entry & ~0xFFF,
                                 &error_abort);
        object_property_set_uint(OBJECT(cpu), "pauth-mlo", m_lo, &error_abort);
        object_property_set_uint(OBJECT(cpu), "pauth-mhi", m_hi, &error_abort);
        if (tcpu->cpu_id == 0) {
            async_run_on_cpu(cpu, t8030_cpu_reset_work,
                             RUN_ON_CPU_HOST_PTR(t8030_machine));
            continue;
        }
        if (ARM_CPU(cpu)->power_state != PSCI_OFF) {
            arm_reset_cpu(tcpu->mpidr);
        }
    }
}

static void t8030_machine_reset(MachineState *machine, ShutdownCause reason)
{
    T8030MachineState *t8030_machine = T8030_MACHINE(machine);
    DeviceState *gpio = NULL;

    qemu_devices_reset(reason);
    memset(&t8030_machine->pmgr_reg, 0, sizeof(t8030_machine->pmgr_reg));
    if (!runstate_check(RUN_STATE_RESTORE_VM) &&
        !runstate_check(RUN_STATE_PRELAUNCH)) {
        if (!runstate_check(RUN_STATE_PAUSED) ||
            reason != SHUTDOWN_CAUSE_NONE) {
            t8030_memory_setup(MACHINE(t8030_machine));
        }
    }
    t8030_cpu_reset(t8030_machine);
    gpio =
        DEVICE(object_property_get_link(OBJECT(machine), "gpio", &error_fatal));

    qemu_set_irq(qdev_get_gpio_in(gpio, T8030_GPIO_FORCE_DFU),
                 t8030_machine->force_dfu);
}

static void t8030_machine_init_done(Notifier *notifier, void *data)
{
    T8030MachineState *t8030_machine =
        container_of(notifier, T8030MachineState, init_done_notifier);
    t8030_memory_setup(MACHINE(t8030_machine));
    t8030_cpu_reset(t8030_machine);
}

static void t8030_machine_init(MachineState *machine)
{
    T8030MachineState *t8030_machine;
    MachoHeader64 *hdr;
    uint64_t kernel_low = 0, kernel_high = 0;
    uint32_t build_version;
    uint32_t data;
    uint64_t data64;
    uint8_t buffer[0x40];
    DTBNode *child;
    DTBProp *prop;
    hwaddr *ranges;

    bzero(buffer, sizeof(buffer));

    t8030_machine = T8030_MACHINE(machine);

    if (!t8030_machine->sep_fw_filename != !t8030_machine->seprom_filename) {
        error_setg(&error_abort,
                   "You need to specify both the SEPROM and the decrypted "
                   "SEPFW in order to use SEP emulation!");
        return;
    }

    t8030_machine->sysmem = get_system_memory();
    allocate_ram(t8030_machine->sysmem, "SROM", T8030_SROM_BASE,
                 T8030_SROM_SIZE, 0);
    allocate_ram(t8030_machine->sysmem, "SRAM", T8030_SRAM_BASE,
                 T8030_SRAM_SIZE, 0);
    allocate_ram(t8030_machine->sysmem, "DRAM", T8030_DRAM_BASE,
                 T8030_DRAM_SIZE, 0);
    allocate_ram(t8030_machine->sysmem, "SEPROM", T8030_SEPROM_BASE,
                 T8030_SEPROM_SIZE, 0);
    allocate_ram(t8030_machine->sysmem, "DRAM_3", 0x300000000ULL,
                 0x100000000ULL, 0);

    hdr = macho_load_file(machine->kernel_filename, NULL);
    g_assert(hdr);
    t8030_machine->kernel = hdr;
    build_version = macho_build_version(hdr);
    info_report("Loading %s %u.%u...", macho_platform_string(hdr),
                BUILD_VERSION_MAJOR(build_version),
                BUILD_VERSION_MINOR(build_version));
    t8030_machine->build_version = build_version;

    switch (BUILD_VERSION_MAJOR(build_version)) {
    case 13:
        t8030_machine->rtbuddy_protocol_ver = 10;
        break;
    case 14:
        t8030_machine->rtbuddy_protocol_ver = 11;
        break;
    case 15:
        QEMU_FALLTHROUGH;
    case 16:
        QEMU_FALLTHROUGH;
    case 17:
        QEMU_FALLTHROUGH;
    case 18:
        t8030_machine->rtbuddy_protocol_ver = 12;
        break;
    default:
        break;
    }

    macho_highest_lowest(hdr, &kernel_low, &kernel_high);
    info_report("Kernel virtual low: 0x" TARGET_FMT_lx, kernel_low);
    info_report("Kernel virtual high: 0x" TARGET_FMT_lx, kernel_high);

    g_virt_base = kernel_low;
    g_phys_base = (hwaddr)macho_get_buffer(hdr);

    t8030_patch_kernel(hdr);

    t8030_machine->device_tree = load_dtb_from_file(machine->dtb);
    t8030_machine->trustcache =
        load_trustcache_from_file(t8030_machine->trustcache_filename,
                                  &t8030_machine->bootinfo.trustcache_size);
    data = 24000000;
    set_dtb_prop(t8030_machine->device_tree, "clock-frequency", sizeof(data),
                 &data);
    child = find_dtb_node(t8030_machine->device_tree, "arm-io");
    g_assert(child);

    data = 0x20;
    set_dtb_prop(child, "chip-revision", sizeof(data), &data);

    set_dtb_prop(child, "clock-frequencies", sizeof(clock_freq), clock_freq);

    prop = find_dtb_prop(child, "ranges");
    g_assert(prop);

    ranges = (hwaddr *)prop->value;
    t8030_machine->soc_base_pa = ranges[1];
    t8030_machine->soc_size = ranges[2];

    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "t8030", 5);
    set_dtb_prop(t8030_machine->device_tree, "platform-name", 32, buffer);
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "MWL72", 5);
    set_dtb_prop(t8030_machine->device_tree, "model-number", 32, buffer);
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "LL/A", 4);
    set_dtb_prop(t8030_machine->device_tree, "region-info", 32, buffer);
    memset(buffer, 0, sizeof(buffer));
    set_dtb_prop(t8030_machine->device_tree, "config-number", 0x40, buffer);
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "C39ZRMDEN72J", 12);
    set_dtb_prop(t8030_machine->device_tree, "serial-number", 32, buffer);
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "C39948108J9N72J1F", 17);
    set_dtb_prop(t8030_machine->device_tree, "mlb-serial-number", 32, buffer);
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "A2111", 5);
    set_dtb_prop(t8030_machine->device_tree, "regulatory-model-number", 32,
                 buffer);

    child = get_dtb_node(t8030_machine->device_tree, "chosen");
    // TODO: basic AGX emulation, because on t8030 it does not expect
    // us to be using the SW renderer and messes up. also gives us an AGX
    // twiddled texture which we don't know how to handle yet.
    data = 0x8015;
    set_dtb_prop(child, "chip-id", 4, &data);
    data = 0x4; // board-id ; match with apple_aes.c
    set_dtb_prop(child, "board-id", 4, &data);

    if (t8030_machine->ecid == 0) {
        t8030_machine->ecid = 0x1122334455667788;
    }
    set_dtb_prop(child, "unique-chip-id", 8, &t8030_machine->ecid);

    // Update the display parameters
    data = 0;
    set_dtb_prop(child, "display-rotation", sizeof(data), &data);
    data = 2;
    set_dtb_prop(child, "display-scale", sizeof(data), &data);

    child = get_dtb_node(t8030_machine->device_tree, "product");
    data64 = 0x100000027;
    g_assert(
        set_dtb_prop(child, "display-corner-radius", sizeof(data64), &data64));
    data = 0x1;
    g_assert(set_dtb_prop(child, "oled-display", sizeof(data), &data));
    // TODO: PMP
    g_assert(set_dtb_prop(t8030_machine->device_tree, "target-type", 4, "sim"));
    data = 0;
    g_assert(set_dtb_prop(child, "device-color-policy", sizeof(data), &data));

    t8030_cpu_setup(machine);

    t8030_create_aic(machine);

    for (int i = 0; i < T8030_NUM_UARTS; i++) {
        t8030_create_s3c_uart(t8030_machine, i, serial_hd(i));
    }

    t8030_pmgr_setup(machine);
    t8030_amcc_setup(machine);

    t8030_create_ans(machine);

    t8030_create_gpio(machine, "gpio");
    t8030_create_gpio(machine, "smc-gpio");
    t8030_create_gpio(machine, "nub-gpio");

    t8030_create_i2c(machine, "i2c0");
    t8030_create_i2c(machine, "i2c1");
    t8030_create_i2c(machine, "i2c2");
    t8030_create_i2c(machine, "i2c3");
    t8030_create_i2c(machine, "smc-i2c0");
    t8030_create_i2c(machine, "smc-i2c1");

    t8030_create_dart(machine, "dart-usb");
    t8030_create_dart(machine, "dart-sio");
    t8030_create_dart(machine, "dart-disp0");
    t8030_create_dart(machine, "dart-sep");
    t8030_create_usb(machine);

    t8030_create_wdt(machine);

    t8030_create_aes(machine);

    t8030_create_spmi(machine, "spmi0");
    t8030_create_spmi(machine, "spmi1");
    t8030_create_spmi(machine, "spmi2");

    t8030_create_pmu(machine, "spmi0", "spmi-pmu");

    t8030_create_smc(machine);
    t8030_create_sio(machine);

    for (int i = 0; i < T8030_NUM_SPIS; i++) {
        t8030_create_spi(machine, i);
    }

    if (t8030_machine->seprom_filename && t8030_machine->sep_fw_filename) {
        t8030_create_sep(machine);
    } else {
        t8030_create_sep_sim(machine);
    }

    t8030_roswell_create(machine);

    t8030_display_create(machine);

    t8030_machine->init_done_notifier.notify = t8030_machine_init_done;
    qemu_add_machine_init_done_notifier(&t8030_machine->init_done_notifier);
}

static void t8030_set_trustcache_filename(Object *obj, const char *value,
                                          Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    g_free(t8030_machine->trustcache_filename);
    t8030_machine->trustcache_filename = g_strdup(value);
}

static char *t8030_get_trustcache_filename(Object *obj, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    return g_strdup(t8030_machine->trustcache_filename);
}

static void t8030_set_ticket_filename(Object *obj, const char *value,
                                      Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    g_free(t8030_machine->ticket_filename);
    t8030_machine->ticket_filename = g_strdup(value);
}

static char *t8030_get_ticket_filename(Object *obj, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    return g_strdup(t8030_machine->ticket_filename);
}

static void t8030_set_seprom_filename(Object *obj, const char *value,
                                      Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    g_free(t8030_machine->seprom_filename);
    t8030_machine->seprom_filename = g_strdup(value);
}

static char *t8030_get_seprom_filename(Object *obj, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    return g_strdup(t8030_machine->seprom_filename);
}

static void t8030_set_sepfw_filename(Object *obj, const char *value,
                                     Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    g_free(t8030_machine->sep_fw_filename);
    t8030_machine->sep_fw_filename = g_strdup(value);
}

static char *t8030_get_sepfw_filename(Object *obj, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    return g_strdup(t8030_machine->sep_fw_filename);
}

static void t8030_set_boot_mode(Object *obj, const char *value, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    if (g_str_equal(value, "auto")) {
        t8030_machine->boot_mode = kBootModeAuto;
    } else if (g_str_equal(value, "manual")) {
        t8030_machine->boot_mode = kBootModeManual;
    } else if (g_str_equal(value, "enter_recovery")) {
        t8030_machine->boot_mode = kBootModeEnterRecovery;
    } else if (g_str_equal(value, "exit_recovery")) {
        t8030_machine->boot_mode = kBootModeExitRecovery;
    } else {
        t8030_machine->boot_mode = kBootModeAuto;
        error_setg(errp, "Invalid boot mode: %s", value);
    }
}

static char *t8030_get_boot_mode(Object *obj, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    switch (t8030_machine->boot_mode) {
    case kBootModeManual:
        return g_strdup("manual");
    case kBootModeEnterRecovery:
        return g_strdup("enter_recovery");
    case kBootModeExitRecovery:
        return g_strdup("exit_recovery");
    case kBootModeAuto:
        QEMU_FALLTHROUGH;
    default:
        return g_strdup("auto");
    }
}

static void t8030_get_ecid(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    T8030MachineState *t8030_machine;
    int64_t value;

    t8030_machine = T8030_MACHINE(obj);
    value = t8030_machine->ecid;
    visit_type_int(v, name, &value, errp);
}

static void t8030_set_ecid(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    T8030MachineState *t8030_machine;
    int64_t value;

    t8030_machine = T8030_MACHINE(obj);

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    t8030_machine->ecid = value;
}

static void t8030_set_kaslr_off(Object *obj, bool value, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    t8030_machine->kaslr_off = value;
}

static bool t8030_get_kaslr_off(Object *obj, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    return t8030_machine->kaslr_off;
}

static ram_addr_t t8030_machine_fixup_ram_size(ram_addr_t size)
{
    g_assert(size == T8030_DRAM_SIZE);
    return size;
}

static void t8030_set_force_dfu(Object *obj, bool value, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    t8030_machine->force_dfu = value;
}

static bool t8030_get_force_dfu(Object *obj, Error **errp)
{
    T8030MachineState *t8030_machine;

    t8030_machine = T8030_MACHINE(obj);
    return t8030_machine->force_dfu;
}

static void t8030_machine_class_init(ObjectClass *klass, void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);

    mc->desc = "T8030";
    mc->init = t8030_machine_init;
    mc->reset = t8030_machine_reset;
    mc->max_cpus = A13_MAX_CPU + 1;
    mc->no_sdcard = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
    mc->default_cpu_type = TYPE_APPLE_A13;
    mc->minimum_page_bits = 14;
    mc->default_ram_size = T8030_DRAM_SIZE;
    mc->fixup_ram_size = t8030_machine_fixup_ram_size;

    object_class_property_add_str(klass, "trustcache",
                                  t8030_get_trustcache_filename,
                                  t8030_set_trustcache_filename);
    object_class_property_set_description(klass, "trustcache",
                                          "Trustcache to be loaded");
    object_class_property_add_str(klass, "ticket", t8030_get_ticket_filename,
                                  t8030_set_ticket_filename);
    object_class_property_set_description(klass, "ticket",
                                          "APTicket to be loaded");
    object_class_property_add_str(klass, "seprom", t8030_get_seprom_filename,
                                  t8030_set_seprom_filename);
    object_class_property_set_description(klass, "seprom",
                                          "SEPROM to be loaded");
    object_class_property_add_str(klass, "sepfw", t8030_get_sepfw_filename,
                                  t8030_set_sepfw_filename);
    object_class_property_set_description(klass, "sepfw", "SEPFW to be loaded");
    object_class_property_add_str(klass, "boot-mode", t8030_get_boot_mode,
                                  t8030_set_boot_mode);
    object_class_property_set_description(klass, "boot-mode",
                                          "Boot mode of the machine");
    object_class_property_add(klass, "ecid", "uint64", t8030_get_ecid,
                              t8030_set_ecid, NULL, NULL);
    object_class_property_set_description(klass, "ecid", "Device ECID");
    object_class_property_add_bool(klass, "kaslr-off", t8030_get_kaslr_off,
                                   t8030_set_kaslr_off);
    object_class_property_set_description(klass, "kaslr-off", "Disable KASLR");
    object_class_property_add_bool(klass, "force-dfu", t8030_get_force_dfu,
                                   t8030_set_force_dfu);
    object_class_property_set_description(klass, "force-dfu", "Force DFU");
}

static const TypeInfo t8030_machine_info = {
    .name = TYPE_T8030_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(T8030MachineState),
    .class_size = sizeof(T8030MachineClass),
    .class_init = t8030_machine_class_init,
};

static void t8030_machine_types(void)
{
    type_register_static(&t8030_machine_info);
}

type_init(t8030_machine_types)
