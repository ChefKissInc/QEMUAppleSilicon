/*
 * Apple Display Pipe V4 Controller.
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
#include "hw/display/apple_displaypipe_v4.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "system/dma.h"
#include "ui/console.h"
#include "framebuffer.h"

#ifdef CONFIG_PIXMAN
#include "pixman.h"
#else
#error "Pixman support is required"
#endif

#ifdef CONFIG_PNG
#include <png.h>
#else
#error "PNG support is required"
#endif

#if 0
#define ADP_INFO(fmt, ...) fprintf(stderr, fmt "\n", __VA_ARGS__)
#else
#define ADP_INFO(fmt, ...) \
    do {                   \
    } while (0);
#endif

/**
 * Block Bases (DisplayTarget5)
 * 0x08000  |  M3 Control Mailbox
 * 0x0A000  |  M3 Video Mode Mailbox
 * 0x40000  |  Control
 * 0x48000  |  Vertical Frame Timing Generator
 * 0x50000  |  Generic Pixel Pipe 0
 * 0x58000  |  Generic Pixel Pipe 1
 * 0x60000  |  Blend Unit
 * 0x70000  |  White Point Correction
 * 0x7C000  |  Pixel Response Correction
 * 0x80000  |  Dither
 * 0x82000  |  Dither: Enchanced ST Dither 0
 * 0x83000  |  Dither: Enchanced ST Dither 1
 * 0x84000  |  Content-Dependent Frame Duration
 * 0x88000  |  SPLR (Sub-Pixel Layout R?)
 * 0x90000  |  Burn-In Compensation Sampler
 * 0x98000  |  Sub-Pixel Uniformity Correction
 * 0xA0000  |  PDC (Panel Dither Correction?)
 * 0xB0000  |  PCC (Pixel Color Correction?)
 * 0xD0000  |  PCC Mailbox
 * 0xF0000  |  DBM (Dynamic Backlight Modulation?)
 */

/**
 * Interrupt Indices
 * 0 | Maybe VBlank
 * 1 | APT
 * 2 | Maybe GP0
 * 3 | Maybe GP1
 * 4 | ?
 * 5 | ?
 * 6 | ?
 * 7 | ?
 * 8 | M3
 * 9 | ?
 */

#define ADP_V4_GP_COUNT (2)

typedef struct {
    AddressSpace *dma_as;
    uint8_t index;
    uint16_t disp_width;
    uint16_t disp_height;
    uint32_t config_control;
    uint32_t pixel_format;
    uint16_t frame_width;
    uint16_t frame_height;
    uint32_t base;
    uint32_t end;
    uint32_t stride;
    uint16_t width;
    uint16_t height;
    uint8_t *buf;
    uint32_t buf_len;
    uint32_t max_buf_len;
    bool dirty;
} ADPV4GenPipeState;

typedef struct {
    uint32_t layer_config[ADP_V4_GP_COUNT];
    bool dirty;
} ADPV4BlendUnitState;

struct AppleDisplayPipeV4State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    QemuMutex lock;
    uint32_t width;
    uint32_t height;
    pixman_image_t *disp_image;
    MemoryRegion up_regs;
    MemoryRegion *vram_mr;
    hwaddr vram_off;
    uint64_t vram_size;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    MemoryRegionSection vram_section;
    qemu_irq irqs[9];
    uint32_t int_status;
    ADPV4GenPipeState generic_pipe[ADP_V4_GP_COUNT];
    ADPV4BlendUnitState blend_unit;
    QemuConsole *console;
    QEMUBH *update_disp_image_bh;
    bool invalidated;
    uint32_t *boot_splash;
    pixman_image_t *boot_splash_image;
    uint32_t boot_splash_width;
    uint32_t boot_splash_height;
    uint32_t boot_splash_size;
    QEMUTimer *boot_splash_timer;
};

#define REG_CONTROL_INT_STATUS (0x45818)
#define CONTROL_INT_STATUS_MODE_CHANGED BIT(1)
#define CONTROL_INT_STATUS_DISP_UNDERRUN BIT(3)
#define CONTROL_INT_STATUS_VBLANK BIT(10) // "Swap Done"
#define CONTROL_INT_STATUS_SUB_FRAME_OVERFLOW BIT(11)
#define CONTROL_INT_STATUS_M3 BIT(13)
#define CONTROL_INT_STATUS_PCC BIT(17)
// #define CONTROL_INT_STATUS_?? BIT(19) // "Start accumulating"
// #define CONTROL_INT_STATUS_?? BIT(20) // "Frame Processed"
#define CONTROL_INT_STATUS_AXI_READ_ERR BIT(30)
#define CONTROL_INT_STATUS_AXI_WRITE_ERR BIT(31)
#define REG_CONTROL_VERSION (0x46020)
#define CONTROL_VERSION_A0 (0x70044)
#define CONTROL_VERSION_A1 (0x70045)
#define REG_CONTROL_FRAME_SIZE (0x4603C)
#define REG_CONTROL_CONFIG (0x46040)
#define REG_CONTROL_OUT_FIFO_CLK_GATE (0x46074)
#define REG_CONTROL_OUT_FIFO_DEPTH (0x46084)
#define REG_CONTROL_COMPRESSION_CFG (0x460E0)
#define REG_CONTROL_BACKPRESSURE (0x46120)
#define REG_CONTROL_POWER_GATE_CTRL (0x46158)
#define REG_CONTROL_BIS_UPDATE_INTERVAL (0x46198)
#define REG_CONTROL_MIN_BANDWIDTH_RATE (0x461C0)
#define REG_CONTROL_BANDWIDTH_RATE_SCALE_FACTOR (0x461C4)
#define REG_CONTROL_PIO_DMA_BANDWIDTH_RATE (0x461C8)
#define REG_CONTROL_REPLAY_DMA_BANDWIDTH_RATE (0x461CC)
#define REG_CONTROL_GATE_CONTROL (0x461D0)
#define REG_CONTROL_READ_LINK_GATE_METRIC (0x461D4)
#define REG_CONTROL_READ_LTR_CONFIG (0x461D8)
#define REG_CONTROL_LTR_TIMER (0x461DC)
#define REG_CONTROL_WRITE_LTR_CONFIG (0x461E0)

#define GP_BLOCK_BASE (0x50000)
#define GP_BLOCK_SIZE (0x8000)
#define REG_GP_CONFIG_CONTROL (0x4)
#define GP_CONFIG_CONTROL_RUN BIT(0)
#define GP_CONFIG_CONTROL_USE_DMA BIT(18)
#define GP_CONFIG_CONTROL_HDR BIT(24)
#define GP_CONFIG_CONTROL_ENABLED BIT(31)
#define REG_GP_PIXEL_FORMAT (0x0001C)
#define GP_PIXEL_FORMAT_BGRA ((BIT(4) << 22) | BIT(24) | (3 << 13))
#define GP_PIXEL_FORMAT_ARGB ((BIT(4) << 22) | BIT(24))
#define GP_PIXEL_FORMAT_COMPRESSED BIT(30)
#define REG_GP_BASE (0x30)
#define REG_GP_END (0x40)
#define REG_GP_STRIDE (0x60)
#define REG_GP_SIZE (0x70)
#define REG_GP_FRAME_SIZE (0x80)
#define REG_GP_CRC_DATA (0x160)
#define REG_GP_BANDWIDTH_RATE (0x170)
#define REG_GP_STATUS (0x184)
#define GP_STATUS_DECOMPRESSION_FAIL BIT(0)

#define GP_BLOCK_BASE_FOR(i) (GP_BLOCK_BASE + i * GP_BLOCK_SIZE)
#define GP_BLOCK_END_FOR(i) (GP_BLOCK_BASE_FOR(i) + (GP_BLOCK_SIZE - 1))

#define BLEND_BLOCK_BASE (0x60000)
#define BLEND_BLOCK_SIZE (0x8000)
#define REG_BLEND_CONFIG (0x4)
#define REG_BLEND_BG (0x8)
#define REG_BLEND_LAYER_0_BG (0xC)
#define REG_BLEND_LAYER_1_BG (0x10)
#define REG_BLEND_LAYER_0_CONFIG (0x14)
#define REG_BLEND_LAYER_1_CONFIG (0x18)
#define BLEND_LAYER_CONFIG_PIPE(v) ((v) & 0xF)
#define BLEND_LAYER_CONFIG_MODE(v) ((v >> 4) & 0xF)
#define BLEND_MODE_NONE 0
#define BLEND_MODE_ALPHA 1
#define BLEND_MODE_PREMULT 2
#define BLEND_MODE_BYPASS 3
#define REG_BLEND_GAMMA_TABLE_R (0x1C)
#define REG_BLEND_GAMMA_TABLE_G (0x1024)
#define REG_BLEND_GAMMA_TABLE_B (0x202C)
// #define REG_BLEND_?? (0x3034)

static void adp_v4_update_irqs(AppleDisplayPipeV4State *s)
{
    qemu_set_irq(s->irqs[0], (s->int_status & CONTROL_INT_STATUS_VBLANK) != 0);
}

static pixman_format_code_t adp_v4_gp_fmt_to_pixman(ADPV4GenPipeState *s)
{
    if ((s->pixel_format & GP_PIXEL_FORMAT_BGRA) == GP_PIXEL_FORMAT_BGRA) {
        ADP_INFO("gp%d: pixel format is BGRA (0x%X).", s->index,
                 s->pixel_format);
        return PIXMAN_b8g8r8a8;
    } else if ((s->pixel_format & GP_PIXEL_FORMAT_ARGB) ==
               GP_PIXEL_FORMAT_ARGB) {
        ADP_INFO("gp%d: pixel format is ARGB (0x%X).", s->index,
                 s->pixel_format);
        return PIXMAN_a8r8g8b8;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gp%d: pixel format is unknown (0x%X).\n", s->index,
                      s->pixel_format);
        return 0;
    }
}

static void adp_v4_gp_read(ADPV4GenPipeState *s)
{
    uint64_t buf_len;

    // TODO: Decompress the data and display it properly.
    if (s->pixel_format & GP_PIXEL_FORMAT_COMPRESSED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gp%d: dropping frame as it's compressed.\n", s->index);
        s->buf_len = 0;
        return;
    }

    ADP_INFO("gp%d: width and height is %dx%d.", s->index, s->width, s->height);
    ADP_INFO("gp%d: stride is %d.", s->index, s->stride);

    if (s->height == 0 || s->width == 0 || s->stride == 0) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "gp%d: dropping frame as width, height or stride is zero.\n",
            s->index);
        s->buf_len = 0;
        return;
    }

    if (s->width > s->disp_width || s->height > s->disp_height) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gp%d: dropping frame as it's larger than the screen.\n",
                      s->index);
        s->buf_len = 0;
        return;
    }

    buf_len = s->height * s->stride;
    if (s->max_buf_len < buf_len) {
        g_free(s->buf);
        s->buf = g_malloc(buf_len);
        s->max_buf_len = buf_len;
    }
    if (dma_memory_read(s->dma_as, s->base, s->buf, buf_len,
                        MEMTXATTRS_UNSPECIFIED) == MEMTX_OK) {
        s->buf_len = buf_len;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "gp%d: failed to read from DMA.\n",
                      s->index);
        s->buf_len = 0;
    }
}

static void adp_v4_gp_reg_write(ADPV4GenPipeState *s, hwaddr addr,
                                uint64_t data)
{
    switch (addr) {
    case REG_GP_CONFIG_CONTROL: {
        ADP_INFO("gp%d: control <- 0x" HWADDR_FMT_plx, s->index, data);
        s->config_control = (uint32_t)data;
        if (s->config_control & GP_CONFIG_CONTROL_RUN) {
            adp_v4_gp_read(s);
            s->dirty = true;
        }
        break;
    }
    case REG_GP_PIXEL_FORMAT: {
        ADP_INFO("gp%d: pixel format <- 0x" HWADDR_FMT_plx, s->index, data);
        s->pixel_format = (uint32_t)data;
        break;
    }
    case REG_GP_BASE: {
        ADP_INFO("gp%d: base <- 0x" HWADDR_FMT_plx, s->index, data);
        s->base = (uint32_t)data;
        break;
    }
    case REG_GP_END: {
        ADP_INFO("gp%d: end <- 0x" HWADDR_FMT_plx, s->index, data);
        s->end = (uint32_t)data;
        break;
    }
    case REG_GP_STRIDE: {
        ADP_INFO("gp%d: stride <- 0x" HWADDR_FMT_plx, s->index, data);
        s->stride = (uint32_t)data;
        break;
    }
    case REG_GP_SIZE: {
        ADP_INFO("gp%d: size <- 0x" HWADDR_FMT_plx, s->index, data);
        s->height = data & 0xFFFF;
        s->width = (data >> 16) & 0xFFFF;
        break;
    }
    case REG_GP_FRAME_SIZE: {
        ADP_INFO("gp%d: frame size <- 0x" HWADDR_FMT_plx, s->index, data);
        s->frame_height = data & 0xFFFF;
        s->frame_width = (data >> 16) & 0xFFFF;
        break;
    }
    default: {
        ADP_INFO("gp%d: unknown @ 0x" HWADDR_FMT_plx " <- 0x" HWADDR_FMT_plx,
                 s->index, addr, data);
        break;
    }
    }
}

static uint32_t adp_v4_gp_reg_read(ADPV4GenPipeState *s, hwaddr addr)
{
    switch (addr) {
    case REG_GP_CONFIG_CONTROL: {
        ADP_INFO("gp%d: control -> 0x%x", s->index, s->config_control);
        return s->config_control;
    }
    case REG_GP_PIXEL_FORMAT: {
        ADP_INFO("gp%d: pixel format -> 0x%x", s->index, s->pixel_format);
        return s->pixel_format;
    }
    case REG_GP_BASE: {
        ADP_INFO("gp%d: base -> 0x%x", s->index, s->base);
        return s->base;
    }
    case REG_GP_END: {
        ADP_INFO("gp%d: end -> 0x%x", s->index, s->end);
        return s->end;
    }
    case REG_GP_STRIDE: {
        ADP_INFO("gp%d: stride -> 0x%x", s->index, s->stride);
        return s->stride;
    }
    case REG_GP_SIZE: {
        ADP_INFO("gp%d: size -> 0x%x", s->index, (s->width << 16) | s->height);
        return (s->width << 16) | s->height;
    }
    case REG_GP_FRAME_SIZE: {
        ADP_INFO("gp%d: frame size -> 0x%x (width: %d height: %d)", s->index,
                 (s->width << 16) | s->height, s->width, s->height);
        return (s->frame_width << 16) | s->frame_height;
    }
    default: {
        ADP_INFO("gp%d: unknown @ 0x" HWADDR_FMT_plx " -> 0x" HWADDR_FMT_plx,
                 s->index, addr, (hwaddr)0);
        return 0;
    }
    }
}

static void adp_v4_gp_reset(ADPV4GenPipeState *s, uint8_t index,
                            AddressSpace *dma_as, uint16_t disp_width,
                            uint16_t disp_height)
{
    g_free(s->buf);
    memset(s, 0, sizeof(*s));
    s->index = index;
    s->dma_as = dma_as;
    s->disp_width = disp_width;
    s->disp_height = disp_height;
}

static void adp_v4_blend_reg_write(ADPV4BlendUnitState *s, uint64_t addr,
                                   uint64_t data)
{
    switch (addr) {
    case REG_BLEND_LAYER_0_CONFIG: {
        ADP_INFO("blend: layer 0 config <- 0x" HWADDR_FMT_plx, data);
        s->dirty |= s->layer_config[0] != (uint32_t)data;
        s->layer_config[0] = (uint32_t)data;
        break;
    }
    case REG_BLEND_LAYER_1_CONFIG: {
        s->dirty |= s->layer_config[1] != (uint32_t)data;
        s->layer_config[1] = (uint32_t)data;
        ADP_INFO("blend: layer 1 config <- 0x" HWADDR_FMT_plx, data);
        break;
    }
    default: {
        ADP_INFO("blend: unknown @ 0x" HWADDR_FMT_plx " <- 0x" HWADDR_FMT_plx,
                 addr, data);
        break;
    }
    }
}

static uint64_t adp_v4_blend_reg_read(ADPV4BlendUnitState *s, uint64_t addr)
{
    switch (addr) {
    case REG_BLEND_LAYER_0_CONFIG: {
        ADP_INFO("blend: layer 0 config -> 0x%X", s->layer_config[0]);
        return s->layer_config[0];
    }
    case REG_BLEND_LAYER_1_CONFIG: {
        ADP_INFO("blend: layer 1 config -> 0x%X", s->layer_config[1]);
        return s->layer_config[1];
    }
    default: {
        ADP_INFO("blend: unknown @ 0x" HWADDR_FMT_plx " -> 0x" HWADDR_FMT_plx,
                 addr, (hwaddr)0);
        return 0;
    }
    }
}

static void adp_v4_blend_reset(ADPV4BlendUnitState *s)
{
    memset(s, 0, sizeof(*s));
}

static void adp_v4_update_disp_image(AppleDisplayPipeV4State *s)
{
    if (!s->blend_unit.dirty && !s->generic_pipe[0].dirty &&
        !s->generic_pipe[1].dirty) {
        return;
    }

    qemu_bh_schedule(s->update_disp_image_bh);
}

static void adp_v4_reg_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    AppleDisplayPipeV4State *s = opaque;

    QEMU_LOCK_GUARD(&s->lock);

    switch (addr >= 0x200000 ? addr - 0x200000 : addr) {
    case REG_CONTROL_INT_STATUS: {
        s->int_status &= ~(uint32_t)data;
        adp_v4_update_irqs(s);
        break;
    }
    case GP_BLOCK_BASE_FOR(0)... GP_BLOCK_END_FOR(0): {
        adp_v4_gp_reg_write(&s->generic_pipe[0], addr - GP_BLOCK_BASE_FOR(0),
                            data);
        adp_v4_update_disp_image(s);
        break;
    }
    case GP_BLOCK_BASE_FOR(1)... GP_BLOCK_END_FOR(1): {
        adp_v4_gp_reg_write(&s->generic_pipe[1], addr - GP_BLOCK_BASE_FOR(1),
                            data);
        adp_v4_update_disp_image(s);
        break;
    }
    case BLEND_BLOCK_BASE ...(BLEND_BLOCK_BASE + BLEND_BLOCK_SIZE): {
        adp_v4_blend_reg_write(&s->blend_unit, addr - BLEND_BLOCK_BASE, data);
        adp_v4_update_disp_image(s);
        break;
    }
    default: {
        ADP_INFO("disp: unknown @ 0x" HWADDR_FMT_plx " <- 0x" HWADDR_FMT_plx,
                 addr, data);
        break;
    }
    }
}

static uint64_t adp_v4_reg_read(void *const opaque, const hwaddr addr,
                                const unsigned size)
{
    AppleDisplayPipeV4State *s = opaque;

    QEMU_LOCK_GUARD(&s->lock);

    switch (addr >= 0x200000 ? addr - 0x200000 : addr) {
    case REG_CONTROL_VERSION: {
        ADP_INFO("disp: version -> 0x%x", CONTROL_VERSION_A0);
        return CONTROL_VERSION_A0;
    }
    case REG_CONTROL_FRAME_SIZE: {
        ADP_INFO("disp: frame size -> 0x%x", (s->width << 16) | s->height);
        return (s->width << 16) | s->height;
    }
    case REG_CONTROL_INT_STATUS: {
        ADP_INFO("disp: int status -> 0x%x", s->int_status);
        return s->int_status;
    }
    case GP_BLOCK_BASE_FOR(0)... GP_BLOCK_END_FOR(0): {
        return adp_v4_gp_reg_read(&s->generic_pipe[0],
                                  addr - GP_BLOCK_BASE_FOR(0));
    }
    case GP_BLOCK_BASE_FOR(1)... GP_BLOCK_END_FOR(1): {
        return adp_v4_gp_reg_read(&s->generic_pipe[1],
                                  addr - GP_BLOCK_BASE_FOR(1));
    }
    case BLEND_BLOCK_BASE ...(BLEND_BLOCK_BASE + BLEND_BLOCK_SIZE): {
        return adp_v4_blend_reg_read(&s->blend_unit, addr - BLEND_BLOCK_BASE);
    }
    default: {
        ADP_INFO("disp: unknown @ 0x" HWADDR_FMT_plx " -> 0x" HWADDR_FMT_plx,
                 addr, (hwaddr)0);
        return 0;
    }
    }
}

static const MemoryRegionOps adp_v4_reg_ops = {
    .write = adp_v4_reg_write,
    .read = adp_v4_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void adp_v4_draw_row(void *opaque, uint8_t *dest, const uint8_t *src,
                            int width, int dest_pitch)
{
    while (width--) {
        uint32_t colour = ldl_le_p(src);
        memcpy(dest, &colour, sizeof(colour));
        src += sizeof(colour);
        dest += sizeof(colour);
    }
}

static void adp_v4_invalidate(void *opaque)
{
    AppleDisplayPipeV4State *s = opaque;

    QEMU_LOCK_GUARD(&s->lock);

    s->invalidated = true;
}

static void adp_v4_gfx_update(void *opaque)
{
    AppleDisplayPipeV4State *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->console);

    int stride = s->width * sizeof(uint32_t);
    int first = 0, last = 0;

    if (s->invalidated) {
        framebuffer_update_memory_section(&s->vram_section, s->vram_mr,
                                          s->vram_off, s->height, stride);
        s->invalidated = false;
    }

    framebuffer_update_display(surface, &s->vram_section, s->width, s->height,
                               stride, stride, 0, 0, adp_v4_draw_row, s, &first,
                               &last);
    if (first >= 0) {
        dpy_gfx_update(s->console, 0, first, s->width, last - first + 1);
    }

    s->int_status |= CONTROL_INT_STATUS_VBLANK;
    adp_v4_update_irqs(s);
}

static const GraphicHwOps adp_v4_ops = {
    .invalidate = adp_v4_invalidate,
    .gfx_update = adp_v4_gfx_update,
};

static void *adp_v4_get_ram_ptr(AppleDisplayPipeV4State *s)
{
    return memory_region_get_ram_ptr(s->vram_mr) + s->vram_off;
}

static void adp_v4_set_dirty(AppleDisplayPipeV4State *s, hwaddr off,
                             hwaddr size)
{
    memory_region_set_dirty(s->vram_mr, s->vram_off + off, size);
}

static void adp_v4_blit_rect_black(AppleDisplayPipeV4State *s, uint16_t width,
                                   uint16_t height)
{
    size_t y;
    hwaddr off;

    for (y = 0; y < height; y += 1) {
        off = y * s->width * sizeof(uint32_t);
        memset(adp_v4_get_ram_ptr(s) + off, 0, s->width * sizeof(uint32_t));
        adp_v4_set_dirty(s, off, width * sizeof(uint32_t));
    }
}

static void adp_v4_update_disp_image_ptr(AppleDisplayPipeV4State *s)
{
    qemu_pixman_image_unref(s->disp_image);
    s->disp_image = pixman_image_create_bits(PIXMAN_a8r8g8b8, s->width,
                                             s->height, adp_v4_get_ram_ptr(s),
                                             s->width * sizeof(uint32_t));

    qemu_pixman_image_unref(s->boot_splash_image);
    s->boot_splash_image = pixman_image_create_bits(
        PIXMAN_a8b8g8r8, s->boot_splash_width, s->boot_splash_height,
        (uint32_t *)s->boot_splash, s->boot_splash_width * 4);
    pixman_transform_t transform;
    pixman_transform_init_identity(&transform);
    double dest_width = (double)s->width / 1.5;
    pixman_transform_scale(
        &transform, NULL,
        pixman_double_to_fixed((double)s->boot_splash_width / dest_width),
        pixman_double_to_fixed((double)s->boot_splash_height / dest_width));
    pixman_image_set_transform(s->boot_splash_image, &transform);
    pixman_image_set_filter(s->boot_splash_image, PIXMAN_FILTER_BEST, NULL, 0);
    pixman_image_set_repeat(s->boot_splash_image, PIXMAN_REPEAT_NONE);
}

static void adp_v4_draw_boot_splash(AppleDisplayPipeV4State *s)
{
    uint32_t dest_width = (double)s->width / 1.5;
    uint32_t dest_x = s->width / 2 - dest_width / 2;
    uint32_t dest_y = s->height / 2 - dest_width / 2;
    pixman_image_composite(PIXMAN_OP_OVER, s->boot_splash_image, NULL,
                           s->disp_image, 0, 0, 0, 0, dest_x, dest_y,
                           dest_width, dest_width);
    for (uint32_t y = 0; y < dest_width; ++y) {
        adp_v4_set_dirty(s,
                         (dest_x + (dest_y + y) * s->width) * sizeof(uint32_t),
                         dest_width * sizeof(uint32_t));
    }
}

static void adp_v4_draw_boot_splash_timer(void *opaque)
{
    AppleDisplayPipeV4State *s = opaque;
    QEMU_LOCK_GUARD(&s->lock);
    adp_v4_draw_boot_splash(s);
}

static void adp_v4_reset_hold(Object *obj, ResetType type)
{
    AppleDisplayPipeV4State *s = APPLE_DISPLAY_PIPE_V4(obj);

    QEMU_LOCK_GUARD(&s->lock);

    s->invalidated = true;
    s->int_status = 0;

    adp_v4_update_irqs(s);

    adp_v4_update_disp_image_ptr(s);

    adp_v4_gp_reset(&s->generic_pipe[0], 0, &s->dma_as, s->width, s->height);
    adp_v4_gp_reset(&s->generic_pipe[1], 1, &s->dma_as, s->width, s->height);
    adp_v4_blend_reset(&s->blend_unit);

    adp_v4_blit_rect_black(s, s->width, s->height);
    adp_v4_draw_boot_splash(s);

    // Workaround for `-v` removing the boot_splash
    timer_mod(s->boot_splash_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                        NANOSECONDS_PER_SECOND / 2);
}

static void adp_v4_realize(DeviceState *dev, Error **errp)
{
    AppleDisplayPipeV4State *s = APPLE_DISPLAY_PIPE_V4(dev);

    QEMU_LOCK_GUARD(&s->lock);

    s->console = graphic_console_init(dev, 0, &adp_v4_ops, s);
    qemu_console_resize(s->console, s->width, s->height);
}

static const Property adp_v4_props[] = {
    DEFINE_PROP_UINT32("width", AppleDisplayPipeV4State, width, 0),
    DEFINE_PROP_UINT32("height", AppleDisplayPipeV4State, height, 0),
};

static const VMStateDescription vmstate_adp_v4_gp = {
    .name = "ADPV4GenPipeState",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT8(index, ADPV4GenPipeState),
            VMSTATE_UINT16(disp_width, ADPV4GenPipeState),
            VMSTATE_UINT16(disp_height, ADPV4GenPipeState),
            VMSTATE_UINT32(config_control, ADPV4GenPipeState),
            VMSTATE_UINT32(pixel_format, ADPV4GenPipeState),
            VMSTATE_UINT16(frame_width, ADPV4GenPipeState),
            VMSTATE_UINT16(frame_height, ADPV4GenPipeState),
            VMSTATE_UINT32(base, ADPV4GenPipeState),
            VMSTATE_UINT32(end, ADPV4GenPipeState),
            VMSTATE_UINT32(stride, ADPV4GenPipeState),
            VMSTATE_UINT16(width, ADPV4GenPipeState),
            VMSTATE_UINT16(height, ADPV4GenPipeState),
            VMSTATE_UINT32(buf_len, ADPV4GenPipeState),
            VMSTATE_UINT32(max_buf_len, ADPV4GenPipeState),
            VMSTATE_VBUFFER_ALLOC_UINT32(buf, ADPV4GenPipeState, 0, NULL,
                                         max_buf_len),
            VMSTATE_BOOL(dirty, ADPV4GenPipeState),
            VMSTATE_END_OF_LIST(),
        },
};

static const VMStateDescription vmstate_adp_v4_blend_unit = {
    .name = "ADPV4BlendUnitState",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32_ARRAY(layer_config, ADPV4BlendUnitState,
                                 ADP_V4_GP_COUNT),
            VMSTATE_BOOL(dirty, ADPV4BlendUnitState),
            VMSTATE_END_OF_LIST(),
        },
};

static const VMStateDescription vmstate_adp_v4 = {
    .name = "AppleDisplayPipeV4State",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32(width, AppleDisplayPipeV4State),
            VMSTATE_UINT32(height, AppleDisplayPipeV4State),
            VMSTATE_UINT32(int_status, AppleDisplayPipeV4State),
            VMSTATE_STRUCT_ARRAY(generic_pipe, AppleDisplayPipeV4State,
                                 ADP_V4_GP_COUNT, 0, vmstate_adp_v4_gp,
                                 ADPV4GenPipeState),
            VMSTATE_STRUCT(blend_unit, AppleDisplayPipeV4State, 0,
                           vmstate_adp_v4_blend_unit, ADPV4BlendUnitState),
            VMSTATE_BOOL(invalidated, AppleDisplayPipeV4State),
            VMSTATE_UINT32(boot_splash_width, AppleDisplayPipeV4State),
            VMSTATE_UINT32(boot_splash_height, AppleDisplayPipeV4State),
            VMSTATE_UINT32(boot_splash_size, AppleDisplayPipeV4State),
            VMSTATE_VBUFFER_ALLOC_UINT32(boot_splash, AppleDisplayPipeV4State,
                                         0, NULL, boot_splash_size),
            VMSTATE_TIMER_PTR(boot_splash_timer, AppleDisplayPipeV4State),
            VMSTATE_END_OF_LIST(),
        },
};

static void adp_v4_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    rc->phases.hold = adp_v4_reset_hold;

    dc->desc = "Apple Display Pipe V4";
    device_class_set_props(dc, adp_v4_props);
    dc->realize = adp_v4_realize;
    dc->vmsd = &vmstate_adp_v4;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo adp_v4_type_info = {
    .name = TYPE_APPLE_DISPLAY_PIPE_V4,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AppleDisplayPipeV4State),
    .class_init = adp_v4_class_init,
};

static void adp_v4_register_types(void)
{
    type_register_static(&adp_v4_type_info);
}

type_init(adp_v4_register_types);

static void adp_v4_blit_single_layer(AppleDisplayPipeV4State *s,
                                     ADPV4GenPipeState *gp)
{
    size_t i;
    hwaddr off;

    for (i = 0; i < gp->height; i += 1) {
        off = i * s->width * sizeof(uint32_t);
        memcpy(adp_v4_get_ram_ptr(s) + off, gp->buf + i * gp->stride,
               gp->width * sizeof(uint32_t));
        adp_v4_set_dirty(s, off, gp->width * sizeof(uint32_t));
    }
    gp->dirty = false;
}

// TODO: Where is the destination X and Y?
static void adp_v4_update_disp_image_bh(void *opaque)
{
    AppleDisplayPipeV4State *s = opaque;
    ADPV4GenPipeState *layer_0_gp;
    ADPV4GenPipeState *layer_1_gp;
    uint8_t layer_0_blend;
    uint8_t layer_1_blend;
    pixman_format_code_t layer_0_fmt;
    pixman_format_code_t layer_1_fmt;
    pixman_image_t *layer_0_img;
    pixman_image_t *layer_1_img;

    QEMU_LOCK_GUARD(&s->lock);

    layer_0_gp = &s->generic_pipe[BLEND_LAYER_CONFIG_PIPE(
        s->blend_unit.layer_config[0])];
    layer_1_gp = &s->generic_pipe[BLEND_LAYER_CONFIG_PIPE(
        s->blend_unit.layer_config[1])];
    layer_0_blend = BLEND_LAYER_CONFIG_MODE(s->blend_unit.layer_config[0]);
    layer_1_blend = BLEND_LAYER_CONFIG_MODE(s->blend_unit.layer_config[1]);

    if ((layer_0_blend | layer_1_blend) == BLEND_MODE_NONE ||
        ((layer_0_gp->config_control | layer_1_gp->config_control) &
         GP_CONFIG_CONTROL_ENABLED) == 0 ||
        (layer_0_gp->buf_len | layer_1_gp->buf_len) == 0) {
        return;
    }

    if (layer_1_blend != BLEND_MODE_NONE &&
        (layer_1_blend == BLEND_MODE_BYPASS ||
         layer_0_blend == BLEND_MODE_NONE)) {
        adp_v4_blit_single_layer(s, layer_1_gp);
    } else if (layer_0_blend != BLEND_MODE_NONE &&
               (layer_0_blend == BLEND_MODE_BYPASS ||
                layer_1_blend == BLEND_MODE_NONE)) {
        adp_v4_blit_single_layer(s, layer_0_gp);
    } else {
        g_assert_false(layer_0_gp == layer_1_gp);

        layer_0_fmt = adp_v4_gp_fmt_to_pixman(layer_0_gp);
        layer_0_img = pixman_image_create_bits(
            layer_0_fmt, layer_0_gp->width, layer_0_gp->height,
            (uint32_t *)layer_0_gp->buf, layer_0_gp->stride);

        layer_1_fmt = adp_v4_gp_fmt_to_pixman(layer_1_gp);
        layer_1_img = pixman_image_create_bits(
            layer_1_fmt, layer_1_gp->width, layer_1_gp->height,
            (uint32_t *)layer_1_gp->buf, layer_1_gp->stride);

        adp_v4_blit_rect_black(s, s->width, s->height);

        pixman_image_composite(PIXMAN_OP_OVER, layer_0_img, NULL, s->disp_image,
                               0, 0, 0, 0, 0, 0, layer_0_gp->frame_width,
                               layer_0_gp->frame_height);
        pixman_image_composite(PIXMAN_OP_OVER, layer_1_img, NULL, s->disp_image,
                               0, 0, 0, 0, 0, 0, layer_1_gp->frame_width,
                               layer_1_gp->frame_height);

        pixman_image_unref(layer_0_img);
        pixman_image_unref(layer_1_img);

        adp_v4_set_dirty(s, 0, s->height * s->width * sizeof(uint32_t));

        layer_0_gp->dirty = false;
        layer_1_gp->dirty = false;
    }

    s->blend_unit.dirty = false;
}

// `display-timing-info`
// w_active, v_back_porch, v_front_porch, v_sync_pulse, h_active, h_back_porch,
// h_front_porch, h_sync_pulse
// FIXME: Unhardcode.
static const uint32_t adp_v4_timing_info[] = { 828, 144, 1, 1, 1792, 1, 1, 1 };

SysBusDevice *adp_v4_from_node(AppleDTNode *node, MemoryRegion *dma_mr)
{
    DeviceState *dev;
    SysBusDevice *sbd;
    AppleDisplayPipeV4State *s;
    AppleDTProp *prop;
    uint64_t *reg;
    int i;

    g_assert_nonnull(node);
    g_assert_nonnull(dma_mr);

    dev = qdev_new(TYPE_APPLE_DISPLAY_PIPE_V4);
    sbd = SYS_BUS_DEVICE(dev);
    s = APPLE_DISPLAY_PIPE_V4(sbd);

    qemu_mutex_init(&s->lock);

    s->update_disp_image_bh =
        aio_bh_new_guarded(qemu_get_aio_context(), adp_v4_update_disp_image_bh,
                           s, &dev->mem_reentrancy_guard);

    apple_dt_set_prop_str(node, "display-target", "DisplayTarget5");
    apple_dt_set_prop(node, "display-timing-info", sizeof(adp_v4_timing_info),
                      adp_v4_timing_info);
    apple_dt_set_prop_u32(node, "bics-param-set", 0xD);
    apple_dt_set_prop_u32(node, "dot-pitch", 326);
    apple_dt_set_prop_null(node, "function-brightness_update");

    s->dma_mr = dma_mr;
    g_assert_nonnull(s->dma_mr);
    g_assert_nonnull(object_property_add_const_link(OBJECT(sbd), "dma_mr",
                                                    OBJECT(s->dma_mr)));
    address_space_init(&s->dma_as, s->dma_mr, "disp0.dma");

    prop = apple_dt_get_prop(node, "reg");
    g_assert_nonnull(prop);
    reg = (uint64_t *)prop->data;
    memory_region_init_io(&s->up_regs, OBJECT(sbd), &adp_v4_reg_ops, sbd,
                          "up.regs", reg[1]);
    sysbus_init_mmio(sbd, &s->up_regs);
    object_property_add_const_link(OBJECT(sbd), "up.regs", OBJECT(&s->up_regs));

    for (i = 0; i < 9; i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }

    // Boot splash
    // Please see `ui/icons/CKBrandingNotice.md`
    char *path = get_relocated_path(
        CONFIG_QEMU_ICONDIR "/hicolor/512x512/apps/CKQEMUBootSplash@2x.png");
    g_assert(path != NULL);
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        error_setg(&error_abort, "Missing emulator branding: %s.", path);
        g_assert_not_reached();
    }
    uint8_t sig[8];
    fread(sig, sizeof(sig), 1, fp);
    if (png_sig_cmp(sig, 0, sizeof(sig)) != 0) {
        error_setg(&error_abort, "Invalid emulator branding: %s.", path);
        g_assert_not_reached();
    }
    g_free(path);
    png_structp png_ptr =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    g_assert(png_ptr != NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    g_assert(info_ptr != NULL);
    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sizeof(sig));

    png_read_info(png_ptr, info_ptr);

    s->boot_splash_width = png_get_image_width(png_ptr, info_ptr);
    s->boot_splash_height = png_get_image_height(png_ptr, info_ptr);

    png_read_update_info(png_ptr, info_ptr);

    s->boot_splash =
        g_new0(uint32_t, s->boot_splash_width * s->boot_splash_height);
    s->boot_splash_size =
        s->boot_splash_width * s->boot_splash_height * sizeof(uint32_t);

    png_bytep *row_ptrs = malloc(sizeof(png_bytep) * s->boot_splash_height);
    for (uint32_t y = 0; y < s->boot_splash_height; y++)
        row_ptrs[y] = (png_bytep)(s->boot_splash + y * s->boot_splash_width);

    png_read_image(png_ptr, row_ptrs);

    g_free(row_ptrs);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    s->boot_splash_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, adp_v4_draw_boot_splash_timer, s);

    return sbd;
}

void adp_v4_update_vram_mapping(AppleDisplayPipeV4State *s, MemoryRegion *mr,
                                hwaddr base, uint64_t size)
{
    s->vram_mr = mr;
    s->vram_off = base;
    s->vram_size = size;
    s->invalidated = true;
}
