/*
 * Apple M2 Scaler and Color Space Converter.
 *
 * Copyright (c) 2026 Visual Ehrmanntraut (VisualEhrmanntraut).
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
#include "hw/display/apple_scaler.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/resettable.h"
#include "qemu/lockable.h"
#include "qemu/thread.h"
#include "system/dma.h"

#ifdef CONFIG_PIXMAN
    #include "pixman.h"
#else
    #error "Pixman support is required"
#endif

#if 0
    #define SCALER_INFO(fmt, ...) fprintf(stderr, "%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
    #define SCALER_INFO(fmt, ...) \
        do { }                    \
        while (0);
#endif

// clang-format off
REG32(GLBL_VER, 0x0)
REG32(GLBL_CTRL, 0x4)
    REG_FIELD(GLBL_CTRL, RESET, 0, 1)
REG32(GLBL_STS, 0xC)
    REG_FIELD(GLBL_STS, RUNNING, 0, 1)
REG32(GLBL_IRQ_MASK, 0x1C)
REG32(GLBL_IRQSTS, 0x20)
REG32(GLBL_FRAMECNT, 0x24)
// End: 0x60

REG32(CTRL_COMMAND, 0x80)
    REG_FIELD(CTRL_COMMAND, RUN, 0, 1)
    REG_FIELD(CTRL_COMMAND, READ_ONLY, 1, 1)
REG32(CTRL_DBG, 0x9C)
REG32(CTRL_PIXEL_AVERAGING, 0xE4)
// End: 0x10C

enum {
    SWIZZLE_COMPONENT_BLUE = 0,
    SWIZZLE_COMPONENT_GREEN = 1,
    SWIZZLE_COMPONENT_RED = 2,
    SWIZZLE_COMPONENT_ALPHA = 3,
};

REG32(SRC_CFG, 0x180)
REG32(SRC_CFG_LUMA_TEXTURE_SIZES, 0x184)
REG32(SRC_CFG_CHROMA_TEXTURE_SIZES, 0x188)
REG32(SRC_CFG_LUMA_BASE, 0x194)
REG32(SRC_CFG_CHROMA_BASE, 0x198)
REG32(SRC_CFG_LUMA_STRIDE, 0x1B4)
REG32(SRC_CFG_CHROMA_STRIDE, 0x1B8)
REG32(SRC_CFG_LUMA_OFFSETS, 0x1BC)
REG32(SRC_CFG_CHROMA_OFFSETS, 0x1C0)
REG32(SRC_CFG_SWIZZLE, 0x1C4)
REG32(SRC_CFG_SIZE, 0x1C8)
// End: 0x1F0

REG32(DST_CFG, 0x280)
REG32(DST_CFG_LUMA_TEXTURE_SIZES, 0x284)
REG32(DST_CFG_CHROMA_TEXTURE_SIZES, 0x288)
REG32(DST_CFG_LUMA_BASE, 0x294)
REG32(DST_CFG_CHROMA_BASE, 0x298)
REG32(DST_CFG_LUMA_STRIDE, 0x2B4)
REG32(DST_CFG_CHROMA_STRIDE, 0x2B8)
REG32(DST_CFG_LUMA_OFFSETS, 0x2BC)
REG32(DST_CFG_CHROMA_OFFSETS, 0x2C0)
REG32(DST_CFG_SWIZZLE, 0x2C4)
REG32(DST_CFG_SIZE, 0x2C8)
// End: 0x2FC

REG_FIELD(SRCDST_CFG_SWIZZLE, COMPONENT_0, 0, 2)
REG_FIELD(SRCDST_CFG_SWIZZLE, COMPONENT_1, 8, 2)
REG_FIELD(SRCDST_CFG_SWIZZLE, COMPONENT_2, 16, 2)
REG_FIELD(SRCDST_CFG_SWIZZLE, COMPONENT_3, 24, 2)
REG_FIELD(SRCDST_CFG_SIZE, WIDTH, 0, 15)
REG_FIELD(SRCDST_CFG_SIZE, HEIGHT, 16, 15)

REG32(FLIP_ROTATE_CFG, 0x380)
    REG_FIELD(FLIP_ROTATE_CFG, ROTATE_90, 0, 1)
    REG_FIELD(FLIP_ROTATE_CFG, ROTATE_180, 1, 1)
    REG_FIELD(FLIP_ROTATE_CFG, FLIP_Y, 2, 1)
    REG_FIELD(FLIP_ROTATE_CFG, FLIP_X, 3, 1)
// End: 0x380

REG32(CSC_CFG_CHROMA_DOWNSAMPLING, 0x900)
// End: 0x900

REG32(BORDER_FILL_CFG, 0x3034)
REG32(BORDER_FILL_CFG_RED_Y, 0x3038)
REG32(BORDER_FILL_CFG_BLUE_CR, 0x303C)
REG32(BORDER_FILL_CFG_GREEN_CB, 0x3040)
REG32(BORDER_FILL_CFG_LUMA_OFFSETS, 0x3044)
    REG_FIELD(BORDER_FILL_CFG_LUMA_OFFSETS, X, 0, 15)
    REG_FIELD(BORDER_FILL_CFG_LUMA_OFFSETS, Y, 16, 15)
REG32(BORDER_FILL_CFG_CHROMA_OFFSETS, 0x3048)
    REG_FIELD(BORDER_FILL_CFG_CHROMA_OFFSETS, X, 0, 15)
    REG_FIELD(BORDER_FILL_CFG_CHROMA_OFFSETS, Y, 16, 15)
// End: 0x3048
// clang-format on

typedef enum
{
    SOURCE,
    DEST,
    SOURCE_DEST_COUNT,
} SourceDest;

typedef enum
{
    LUMA,
    CHROMA,
    LUMA_CHROMA_COUNT,
} LumaChroma;

typedef struct AppleScalerState
{
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    QemuMutex     lock;
    MemoryRegion  regs[2];
    MemoryRegion* dma_mr;
    AddressSpace  dma_as;
    qemu_irq      irqs[2];
    QEMUBH*       bh;

    uint32_t config[SOURCE_DEST_COUNT];
    uint32_t base[SOURCE_DEST_COUNT][LUMA_CHROMA_COUNT];
    uint32_t stride[SOURCE_DEST_COUNT][LUMA_CHROMA_COUNT];
    uint32_t swizzle[SOURCE_DEST_COUNT];
    uint32_t size[SOURCE_DEST_COUNT];
    uint32_t frame_count;
    uint32_t irq_sts;
    bool     running;
} AppleScalerState;

typedef enum
{
    APPLE_SCALER_FORMAT_BGRA = 0,
    APPLE_SCALER_FORMAT_RGBA,
    APPLE_SCALER_FORMAT_YUV_422,
    APPLE_SCALER_FORMAT_YUV_420,
    APPLE_SCALER_FORMAT_UNKNOWN = -1,
} AppleScalerFormat;

#if 0
static const char *apple_scaler_stringify_format(AppleScalerFormat format)
{
    switch (format) {
    case APPLE_SCALER_FORMAT_BGRA:
        return "BGRA";
    case APPLE_SCALER_FORMAT_RGBA:
        return "RGBA";
    case APPLE_SCALER_FORMAT_YUV_422:
        return "YUV_422";
    case APPLE_SCALER_FORMAT_YUV_420:
        return "YUV_420";
    default:
        return "UNKNOWN";
    }
}
#endif

static AppleScalerFormat apple_scaler_convert_hw_format(uint32_t hw_format, uint32_t hw_swizzle)
{
    switch (hw_format) {
        case 0x100604: return APPLE_SCALER_FORMAT_YUV_422;
        case 0x10060C: return APPLE_SCALER_FORMAT_YUV_420;
        case 0x1E00001:
            if (REG_FIELD_EX32(hw_swizzle, SRCDST_CFG_SWIZZLE, COMPONENT_0) == SWIZZLE_COMPONENT_BLUE
                && REG_FIELD_EX32(hw_swizzle, SRCDST_CFG_SWIZZLE, COMPONENT_1) == SWIZZLE_COMPONENT_GREEN
                && REG_FIELD_EX32(hw_swizzle, SRCDST_CFG_SWIZZLE, COMPONENT_2) == SWIZZLE_COMPONENT_RED
                && REG_FIELD_EX32(hw_swizzle, SRCDST_CFG_SWIZZLE, COMPONENT_3) == SWIZZLE_COMPONENT_ALPHA)
            {
                return APPLE_SCALER_FORMAT_BGRA;
            }
            return APPLE_SCALER_FORMAT_RGBA;
        default: return APPLE_SCALER_FORMAT_UNKNOWN;
    }
}

static pixman_format_code_t apple_scaler_format_to_pixman(AppleScalerFormat format)
{
    switch (format) {
        case APPLE_SCALER_FORMAT_BGRA: return PIXMAN_b8g8r8a8;
        case APPLE_SCALER_FORMAT_RGBA: return PIXMAN_r8g8b8a8;
        default                      : assert_not_reached();
    }
}

#if 0
static void apple_scaler_export_file(bool src, uint32_t width, uint32_t height,
                                     AppleScalerFormat format,
                                     const void *contents, uint64_t size)
{
    char fn[128] = { 0 };
    snprintf(fn, sizeof(fn), "/Users/visual/Downloads/Scaler/%s_%lld_%ux%u_%s",
             src ? "src" : "dst", qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), width,
             height, apple_scaler_stringify_format(format));
    g_file_set_contents(fn, contents, size, NULL);
}
#endif

static void apple_scaler_update_irqs(AppleScalerState* scaler) { qemu_set_irq(scaler->irqs[0], scaler->irq_sts != 0); }

static void apple_scaler_signal_frame_done(AppleScalerState* scaler)
{
    scaler->frame_count += 1;
    scaler->irq_sts      = 1;    // ??
    scaler->running      = false;
    apple_scaler_update_irqs(scaler);
}

static void apple_scaler_bh(void* opaque)
{
    AppleScalerState*  scaler = opaque;
    AppleScalerFormat  src_format;
    uint32_t           src_width;
    uint32_t           src_height;
    uint32_t           src_stride;
    uint32_t           src_buf_size;
    void*              src_buf;
    pixman_image_t*    src_image;
    AppleScalerFormat  dst_format;
    uint32_t           dst_width;
    uint32_t           dst_height;
    uint32_t           dst_stride;
    uint32_t           dst_buf_size;
    void*              dst_buf;
    pixman_image_t*    dst_image;
    pixman_transform_t transform = {0};

    QEMU_LOCK_GUARD(&scaler->lock);

    src_format = apple_scaler_convert_hw_format(scaler->config[SOURCE], scaler->swizzle[SOURCE]);
    dst_format = apple_scaler_convert_hw_format(scaler->config[DEST], scaler->swizzle[DEST]);

    // Only RGBA, BGRA supported for now.
    if ((src_format != APPLE_SCALER_FORMAT_BGRA && src_format != APPLE_SCALER_FORMAT_RGBA)
        || (dst_format != APPLE_SCALER_FORMAT_BGRA && dst_format != APPLE_SCALER_FORMAT_RGBA))
    {
        apple_scaler_signal_frame_done(scaler);
        return;
    }

    src_width  = REG_FIELD_EX32(scaler->size[SOURCE], SRCDST_CFG_SIZE, WIDTH);
    src_height = REG_FIELD_EX32(scaler->size[SOURCE], SRCDST_CFG_SIZE, HEIGHT);
    src_stride = scaler->stride[SOURCE][LUMA];
    SCALER_INFO("src w/h %ux%u stride 0x%X", src_width, src_height, src_stride);
    src_buf_size = src_height * src_stride;
    src_buf      = g_malloc(src_buf_size);
    dma_memory_read(&scaler->dma_as, scaler->base[SOURCE][LUMA], src_buf, src_buf_size, MEMTXATTRS_UNSPECIFIED);
    src_image =
        pixman_image_create_bits(apple_scaler_format_to_pixman(src_format), src_width, src_height, src_buf, src_stride);

#if 0
    apple_scaler_export_file(SOURCE, src_width, src_height, src_format, src_buf,
                             src_buf_size);
#endif

    dst_width  = REG_FIELD_EX32(scaler->size[DEST], SRCDST_CFG_SIZE, WIDTH);
    dst_height = REG_FIELD_EX32(scaler->size[DEST], SRCDST_CFG_SIZE, HEIGHT);
    dst_stride = scaler->stride[DEST][LUMA];
    SCALER_INFO("dst w/h %ux%u stride 0x%X", dst_width, dst_height, dst_stride);
    dst_buf_size = dst_height * dst_stride;
    dst_buf      = g_malloc(dst_buf_size);
    dst_image =
        pixman_image_create_bits(apple_scaler_format_to_pixman(dst_format), dst_width, dst_height, dst_buf, dst_stride);

    pixman_transform_init_scale(&transform, pixman_double_to_fixed((double)src_width / dst_width),
                                pixman_double_to_fixed((double)src_height / dst_height));
    pixman_image_set_transform(src_image, &transform);
    pixman_image_set_filter(src_image, PIXMAN_FILTER_BEST, NULL, 0);
    pixman_image_set_repeat(src_image, PIXMAN_REPEAT_NONE);

    pixman_image_composite(PIXMAN_OP_SRC, src_image, NULL, dst_image, 0, 0, 0, 0, 0, 0, dst_width, dst_height);
    pixman_image_unref(dst_image);

#if 0
    apple_scaler_export_file(DEST, dst_width, dst_height, dst_format, dst_buf,
                             dst_buf_size);
#endif

    pixman_image_unref(src_image);
    g_free(src_buf);

    dma_memory_write(&scaler->dma_as, scaler->base[DEST][LUMA], dst_buf, dst_buf_size, MEMTXATTRS_UNSPECIFIED);

    g_free(dst_buf);

    apple_scaler_signal_frame_done(scaler);
}

static void apple_scaler_reg_write(void* opaque, hwaddr addr, uint64_t data, unsigned size)
{
    AppleScalerState* scaler = opaque;
    uint32_t*         reg;

    // SCALER_INFO("0x" HWADDR_FMT_plx " <- 0x" HWADDR_FMT_plx, addr, data);

    switch (addr >> 2) {
        case R_GLBL_IRQSTS:
            scaler->irq_sts &= ~(uint32_t)data;
            qemu_mutex_lock(&scaler->lock);
            apple_scaler_update_irqs(scaler);
            qemu_mutex_unlock(&scaler->lock);
            return;
        case R_GLBL_CTRL:
            if (REG_FIELD_EX32(data, GLBL_CTRL, RESET)) { resettable_reset(OBJECT(scaler), RESET_TYPE_COLD); }
            return;
        case R_SRC_CFG              : reg = &scaler->config[SOURCE]; break;
        case R_SRC_CFG_LUMA_BASE    : reg = &scaler->base[SOURCE][LUMA]; break;
        case R_SRC_CFG_CHROMA_BASE  : reg = &scaler->base[SOURCE][CHROMA]; break;
        case R_SRC_CFG_LUMA_STRIDE  : reg = &scaler->stride[SOURCE][LUMA]; break;
        case R_SRC_CFG_CHROMA_STRIDE: reg = &scaler->stride[SOURCE][CHROMA]; break;
        case R_SRC_CFG_SWIZZLE      : reg = &scaler->swizzle[SOURCE]; break;
        case R_SRC_CFG_SIZE         : reg = &scaler->size[SOURCE]; break;
        case R_DST_CFG              : reg = &scaler->config[DEST]; break;
        case R_DST_CFG_LUMA_BASE    : reg = &scaler->base[DEST][LUMA]; break;
        case R_DST_CFG_CHROMA_BASE  : reg = &scaler->base[DEST][CHROMA]; break;
        case R_DST_CFG_LUMA_STRIDE  : reg = &scaler->stride[DEST][LUMA]; break;
        case R_DST_CFG_CHROMA_STRIDE: reg = &scaler->stride[DEST][CHROMA]; break;
        case R_DST_CFG_SWIZZLE      : reg = &scaler->swizzle[DEST]; break;
        case R_DST_CFG_SIZE         : reg = &scaler->size[DEST]; break;
        case R_CTRL_COMMAND:
            if (REG_FIELD_EX32(data, CTRL_COMMAND, RUN) && !qatomic_cmpxchg(&scaler->running, false, true)) {
                qemu_bh_schedule(scaler->bh);
            }
            return;
        default: {
            return;
        }
    }

    if (!qatomic_read(&scaler->running)) { *reg = (uint32_t)data; }
}

static uint64_t apple_scaler_reg_read(void* opaque, hwaddr addr, unsigned size)
{
    AppleScalerState* scaler = opaque;
    uint32_t          ret;

    switch (addr >> 2) {
        case R_GLBL_VER:
            // SOC 0x8 -> 0x90082/0x9009B/0x900A7
            ret = 0x9009B;
            break;
        case R_GLBL_STS     : ret = qatomic_read(&scaler->running) ? R_GLBL_STS_RUNNING_MASK : 0; break;
        case R_GLBL_IRQSTS  : ret = scaler->irq_sts; break;
        case R_GLBL_FRAMECNT: ret = scaler->frame_count; break;
        default             : ret = 0; break;
    }

    // SCALER_INFO("0x" HWADDR_FMT_plx " -> 0x%X", addr, ret);

    return ret;
}

static const MemoryRegionOps apple_scaler_reg_ops = {
    .write                 = apple_scaler_reg_write,
    .read                  = apple_scaler_reg_read,
    .endianness            = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size  = 4,
    .impl.max_access_size  = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned       = false,
};

static void apple_scaler_unk_reg_write(void* opaque, hwaddr addr, uint64_t data, unsigned size)
{
    // AppleScalerState *scaler = opaque;

    // QEMU_LOCK_GUARD(&scaler->lock);

    // SCALER_INFO("0x" HWADDR_FMT_plx " <- 0x" HWADDR_FMT_plx, addr, data);

    switch (addr) {
        default: {
            break;
        }
    }
}

static uint64_t apple_scaler_unk_reg_read(void* opaque, hwaddr addr, unsigned size)
{
    // AppleScalerState *scaler = opaque;
    uint64_t ret;

    // QEMU_LOCK_GUARD(&scaler->lock);

    switch (addr) {
        default: ret = 0; break;
    }

    // SCALER_INFO("0x" HWADDR_FMT_plx " -> 0x" HWADDR_FMT_plx, addr, ret);

    return ret;
}

static const MemoryRegionOps apple_scaler_unk_reg_ops = {
    .write                 = apple_scaler_unk_reg_write,
    .read                  = apple_scaler_unk_reg_read,
    .endianness            = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size  = 4,
    .impl.max_access_size  = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned       = false,
};

static void apple_scaler_reset_enter(Object* obj, ResetType type)
{
    AppleScalerState* scaler = APPLE_SCALER(obj);

    qemu_mutex_lock(&scaler->lock);
    if (scaler->bh) { qemu_bh_cancel(scaler->bh); }

    memset(scaler->config, 0, sizeof(scaler->config));
    memset(scaler->base, 0, sizeof(scaler->base));
    memset(scaler->stride, 0, sizeof(scaler->stride));
    memset(scaler->swizzle, 0, sizeof(scaler->swizzle));
    memset(scaler->size, 0, sizeof(scaler->size));
    scaler->frame_count = 0;
    scaler->irq_sts     = 0;
    scaler->running     = false;
    qemu_mutex_unlock(&scaler->lock);
}

static void apple_scaler_reset_hold(Object* obj, ResetType type)
{
    AppleScalerState* scaler = APPLE_SCALER(obj);

    qemu_mutex_lock(&scaler->lock);
    apple_scaler_update_irqs(scaler);
    qemu_mutex_unlock(&scaler->lock);
}

static void apple_scaler_realize(DeviceState* dev, Error** errp)
{
    // AppleScalerState *scaler = APPLE_SCALER(dev);
    //
    // QEMU_LOCK_GUARD(&scaler->lock);
}

static void apple_scaler_class_init(ObjectClass* klass, const void* data)
{
    ResettableClass* rc = RESETTABLE_CLASS(klass);
    DeviceClass*     dc = DEVICE_CLASS(klass);

    rc->phases.enter = apple_scaler_reset_enter;
    rc->phases.hold  = apple_scaler_reset_hold;

    dc->realize = apple_scaler_realize;
    dc->desc    = "Apple M2 Scaler and Color Space Converter";
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo apple_scaler_type_info = {
    .name          = TYPE_APPLE_SCALER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AppleScalerState),
    .class_init    = apple_scaler_class_init,
};

static void apple_scaler_register_types(void) { type_register_static(&apple_scaler_type_info); }

type_init(apple_scaler_register_types);

SysBusDevice* apple_scaler_create(AppleDTNode* node, MemoryRegion* dma_mr)
{
    DeviceState*      dev;
    SysBusDevice*     sbd;
    AppleScalerState* scaler;
    AppleDTProp*      prop;
    uint64_t*         reg;
    int               i;

    assert_nonnull(node);
    assert_nonnull(dma_mr);

    dev    = qdev_new(TYPE_APPLE_SCALER);
    sbd    = SYS_BUS_DEVICE(dev);
    scaler = APPLE_SCALER(sbd);

    qemu_mutex_init(&scaler->lock);

    scaler->dma_mr = dma_mr;
    object_property_add_const_link(OBJECT(scaler), "dma_mr", OBJECT(scaler->dma_mr));
    address_space_init(&scaler->dma_as, scaler->dma_mr, "scaler0.dma");

    prop = apple_dt_get_prop(node, "reg");
    assert_nonnull(prop);
    reg = (uint64_t*)prop->data;
    memory_region_init_io(&scaler->regs[0], OBJECT(scaler), &apple_scaler_reg_ops, scaler, "scaler0.regs0", reg[1]);
    memory_region_init_io(&scaler->regs[1], OBJECT(scaler), &apple_scaler_unk_reg_ops, scaler, "scaler0.regs1", reg[3]);
    sysbus_init_mmio(sbd, &scaler->regs[0]);
    sysbus_init_mmio(sbd, &scaler->regs[1]);

    for (i = 0; i < 2; i++) { sysbus_init_irq(sbd, &scaler->irqs[i]); }

    scaler->bh = aio_bh_new(qemu_get_aio_context(), apple_scaler_bh, scaler);

    return sbd;
}
