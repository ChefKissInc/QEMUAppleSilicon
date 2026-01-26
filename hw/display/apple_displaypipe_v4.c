/*
 * Apple Display Pipe V4 Controller.
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
#include "block/aio.h"
#include "hw/display/apple_displaypipe_v4.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "system/dma.h"
#include "ui/console.h"

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
#define ADP_INFO(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
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
    uint8_t index;
    uint32_t config_control;
    uint32_t pixel_format;
    uint16_t dest_width;
    uint16_t dest_height;
    uint32_t data_start;
    uint32_t data_end;
    uint32_t stride;
    uint16_t src_width;
    uint16_t src_height;
    uint8_t *buf;
    uint32_t buf_len;
    uint32_t max_buf_len;
} ADPV4GenPipeState;

static const VMStateDescription vmstate_adp_v4_gp = {
    .name = "ADPV4GenPipeState",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT8(index, ADPV4GenPipeState),
            VMSTATE_UINT32(config_control, ADPV4GenPipeState),
            VMSTATE_UINT32(pixel_format, ADPV4GenPipeState),
            VMSTATE_UINT16(dest_width, ADPV4GenPipeState),
            VMSTATE_UINT16(dest_height, ADPV4GenPipeState),
            VMSTATE_UINT32(data_start, ADPV4GenPipeState),
            VMSTATE_UINT32(data_end, ADPV4GenPipeState),
            VMSTATE_UINT32(stride, ADPV4GenPipeState),
            VMSTATE_UINT16(src_width, ADPV4GenPipeState),
            VMSTATE_UINT16(src_height, ADPV4GenPipeState),
            VMSTATE_UINT32(buf_len, ADPV4GenPipeState),
            VMSTATE_UINT32(max_buf_len, ADPV4GenPipeState),
            VMSTATE_VBUFFER_ALLOC_UINT32(buf, ADPV4GenPipeState, 0, NULL,
                                         max_buf_len),
            VMSTATE_END_OF_LIST(),
        },
};

typedef struct {
    uint32_t layer_config[ADP_V4_GP_COUNT];
} ADPV4BlendUnitState;

static const VMStateDescription vmstate_adp_v4_blend_unit = {
    .name = "ADPV4BlendUnitState",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32_ARRAY(layer_config, ADPV4BlendUnitState,
                                 ADP_V4_GP_COUNT),
            VMSTATE_END_OF_LIST(),
        },
};

struct AppleDisplayPipeV4State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    QemuMutex lock;
    MemoryRegion up_regs;
    uint32_t width;
    uint32_t height;
    MemoryRegion *vram_mr;
    uint64_t vram_off;
    uint64_t vram_size;
    uint64_t fb_off;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    qemu_irq irqs[9];
    uint32_t int_status;
    uint32_t int_enable;
    ADPV4GenPipeState genpipe[ADP_V4_GP_COUNT];
    ADPV4BlendUnitState blend_unit;
    QemuConsole *console;
    QEMUBH *update_disp_image_bh;
    QEMUTimer *boot_splash_timer;
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
            VMSTATE_UINT32(int_enable, AppleDisplayPipeV4State),
            VMSTATE_STRUCT_ARRAY(genpipe, AppleDisplayPipeV4State,
                                 ADP_V4_GP_COUNT, 0, vmstate_adp_v4_gp,
                                 ADPV4GenPipeState),
            VMSTATE_STRUCT(blend_unit, AppleDisplayPipeV4State, 0,
                           vmstate_adp_v4_blend_unit, ADPV4BlendUnitState),
            VMSTATE_TIMER_PTR(boot_splash_timer, AppleDisplayPipeV4State),
            VMSTATE_END_OF_LIST(),
        },
};

// APIODMA
#define REG_APIODMA_CFG (0x40000)
#define REG_APIODMA_CFG_MEM_SRC (0x40010)
#define REG_APIODMA_CFG_MEM_DAT (0x40014)
#define REG_APIODMA_CFG_DMA_CFG_PIO_RD (0x4001C)
#define REG_APIODMA_CFG_DMA_CFG_PIO_WR (0x40020)

// shadow FIFO
#define REG_SHADOW_FIFO_POWER_CONTROL (0x45010)

// pipe control
#define REG_CONTROL_INT_STATUS (0x45818)
#define CONTROL_INT_MODE_CHANGED BIT(1)
#define CONTROL_INT_UNDERRUN BIT(3)
#define CONTROL_INT_OUTPUT_READY BIT(10)
#define CONTROL_INT_SUB_FRAME_OVERFLOW BIT(11)
#define CONTROL_INT_M3 BIT(13)
#define CONTROL_INT_PCC BIT(17)
#define CONTROL_INT_CDFD BIT(19)
#define CONTROL_INT_FRAME_PROCESSED BIT(20)
#define CONTROL_INT_AXI_READ_ERR BIT(30)
#define CONTROL_INT_AXI_WRITE_ERR BIT(31)
#define REG_CONTROL_INT_ENABLE (0x4581C)
#define REG_CONTROL_HILO (0x45830)
#define REG_CONTROL_DISP_FIFO_POWER_CONTROL (0x45834)
#define REG_CONTROL_POWER_GATE_TIMER_GP0 (0x4583C)
#define REG_CONTROL_POWER_GATE_TIMER_GP1 (0x45840)
#define REG_CONTROL_POWER_GATE_TIMER_PPP (0x45844)
#define REG_CONTROL_POWER_GATE_TIMER_HILO (0x45848)

// pipe config
#define REG_CONTROL_PIPE_CONFIG_SPARE_0 (0x46000)
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
#define REG_CONTROL_WR_LTR_CONFIG (0x461E0)
#define REG_CONTROL_RD_PIXEL_LTR_CONV_FACTOR (0x461E4)
#define REG_CONTROL_WR_PIXEL_LTR_CONV_FACTOR (0x461E8)
#define REG_CONTROL_LTR_OFFSET (0x461EC)
#define REG_CONTROL_EVENT_ORDERING (0x46254)
#define REG_CONTROL_MIN_TLTR (0x46258)
#define REG_CONTROL_START_CLTR (0x4625C)
#define REG_CONTROL_OPE_OFFSET0 (0x46268)
#define REG_CONTROL_OPE_OFFSET1 (0x4626C)
#define REG_CONTROL_GP_AGGR_OFFSET0 (0x4627C)
#define REG_CONTROL_GP_AGGR_OFFSET1 (0x46280)
#define REG_CONTROL_BIC_AGGR_OFFSET (0x46284)
#define REG_CONTROL_SPUC_AGGR_OFFSET (0x46288)
#define REG_CONTROL_PDC_RD_AGGR_OFFSET (0x4628C)
#define REG_CONTROL_PREFETCH_BWR_CONFIG (0x46290)
#define REG_CONTROL_PREFETCH_LTR_CONFIG (0x46294)
#define REG_CONTROL_PREFETCH_LTR_PEG_CONFIG (0x46298)
#define REG_CONTROL_COLUMN_REPEAT (0x462D0)
#define REG_CONTROL_LINK_GATE_LB_CONFIG (0x462D4)
#define REG_CONTROL_REPLAY_READ_HEADER_AF_CONFIG (0x462F0)
#define REG_CONTROL_REPLAY_READ_TILE_AF_CONFIG (0x462F4)
#define REG_CONTROL_REPLAY_WRITE_HEADER_AF_CONFIG (0x462F8)
#define REG_CONTROL_REPLAY_WRITE_TILE_AF_CONFIG (0x462FC)
#define REG_CONTROL_DART_AF_CONFIG (0x46300)
#define REG_CONTROL_PIODMA_READ_AF_CONFIG (0x46304)
#define REG_CONTROL_PIODMA_WRITE_AF_CONFIG (0x46308)
#define REG_CONTROL_WRITE_LINK_GATE_METRIC (0x4630C)
#define REG_CONTROL_BIS_WR_AGGR_CONFIGURATION (0x46314)
#define REG_CONTROL_REPLAY_WR_AGGR_CONFIGURATION (0x46318)
#define REG_CONTROL_PDC_WR_AGGR_CONFIGURATION (0x4631C)
#define REG_CONTROL_WR_LINK_GATE_CONFIG (0x46320)
#define REG_CONTROL_WR_LTR_OFFSET_RAMP_CONFIG (0x46324)

#define VFTG_BLOCK_BASE (0x48000)
#define REG_VFTG_DEBUG (0x0)
#define REG_VFTG_CONTROL (0x24)
#define VFTG_CONTROL_RUN BIT(30)
#define REG_VFTG_ACTIVE_SIZE (0x2C)
#define REG_VFTG_FRONT_PORCH (0x30)
#define REG_VFTG_SYNC_PULSE (0x34)
#define REG_VFTG_BACK_PORCH (0x38)
#define REG_VFTG_VBLANK_POSITION (0x44)
#define REG_VFTG_AUTO_IDLE_SUBFRAME_COUNT (0x50)
// 0x200-0x280: IFP_CFG
// 0x300-0x380: BSYNC1
// 0x400-0x480: EB_BSYNC1
// 0x500-0x580: BSYNC0
// 0x600-0x680: EB_BSYNC0
#define REG_VFTG_EB_OFFSET (0x700)
#define REG_VFTG_IDLE_SUB_FRAME_CONFIG (0x708)
#define REG_VFTG_IDLE_SUB_FRAME_TOP_CONFIG (0x70C)
#define REG_VFTG_N1_PARAM (0x780)
#define REG_VFTG_N1_PARAM_VBLANK (0x784)
#define REG_VFTG_N5_A_PARAM (0x788)
#define REG_VFTG_N5_A_PARAM_VBLANK (0x78C)
#define REG_VFTG_N5_B_PARAM (0x790)
#define REG_VFTG_N5_B_PARAM_VBLANK (0x794)
#define REG_VFTG_N6_PARAM (0x798)
#define REG_VFTG_N7_PARAM (0x79C)
#define REG_VFTG_N7_PARAM_VBLANK (0x7A0)
#define REG_VFTG_LINK_UP_TIME_CONFIG (0x7A4)
#define REG_VFTG_AUX_WAKE_CONFIG (0x7A8)
#define REG_VFTG_AUX_DISABLE_TIME (0x7AC)
#define REG_VFTG_POL_OFFSET (0x7C4)
#define REG_VFTG_SWIZZLE_CONFIG_EVEN_ROWS (0x7D0)
#define REG_VFTG_SWIZZLE_CONFIG_EVEN_ROWS_LAST_PAIR (0x7D4)
#define REG_VFTG_SWIZZLE_CONFIG_ODD_ROWS (0x7D8)
#define REG_VFTG_SWIZZLE_CONFIG_ODD_ROWS_LAST_PAIR (0x7DC)
#define REG_VFTG_CROP_CONTROL (0x81C)
#define REG_VFTG_MIPI_MODE (0x828)

#define GP_BLOCK_BASE (0x50000)
#define GP_BLOCK_SIZE (0x8000)
#define REG_GP_CONFIG_CONTROL (0x4)
#define GP_CONFIG_CONTROL_RUN BIT(0)
#define GP_CONFIG_CONTROL_USE_DMA BIT(18)
#define GP_CONFIG_CONTROL_HDR BIT(24)
#define GP_CONFIG_CONTROL_ENABLED BIT(31)
// 0x8, 0xC = ??
// 0x10 = HDR_CONFIG?
#define REG_GP_AF_CONFIG_0 (0x14)
#define REG_GP_AF_CONFIG_1 (0x18)
#define REG_GP_PIXEL_FORMAT (0x1C)
#define GP_PIXEL_FORMAT_BGRA ((BIT(4) << 22) | BIT(24) | (3 << 13))
#define GP_PIXEL_FORMAT_ARGB ((BIT(4) << 22) | BIT(24))
#define GP_PIXEL_FORMAT_COMPRESSED BIT(30)
// 0x20 = ??
#define REG_GP_AGX_CONFIG (0x24)
#define REG_GP_LAYER_0_HTPC_CONFIG (0x28)
#define REG_GP_LAYER_1_HTPC_CONFIG (0x2C)
#define REG_GP_LAYER_0_DATA_START (0x30)
#define REG_GP_LAYER_1_DATA_START (0x34)
#define REG_GP_LAYER_0_DATA_END (0x40)
#define REG_GP_LAYER_1_DATA_END (0x44)
#define REG_GP_LAYER_0_HEADER_BASE (0x48)
#define REG_GP_LAYER_1_HEADER_BASE (0x4C)
#define REG_GP_LAYER_0_HEADER_END (0x58)
#define REG_GP_LAYER_1_HEADER_END (0x5C)
#define REG_GP_LAYER_0_STRIDE (0x60)
#define REG_GP_LAYER_1_STRIDE (0x64)
#define REG_GP_LAYER_0_POSITION (0x68)
#define REG_GP_LAYER_1_POSITION (0x6C)
#define REG_GP_LAYER_0_DIMENSIONS (0x70)
#define REG_GP_LAYER_1_DIMENSIONS (0x74)
#define REG_GP_SRC_POSITION (0x78)
#define REG_GP_DEST_POSITION (0x7C)
#define REG_GP_DEST_DIMENSIONS (0x80)
#define REG_GP_SRC_ACTIVE_REGION_0_POSITION (0x98)
#define REG_GP_SRC_ACTIVE_REGION_1_POSITION (0x9C)
#define REG_GP_SRC_ACTIVE_REGION_2_POSITION (0xA0)
#define REG_GP_SRC_ACTIVE_REGION_3_POSITION (0xA4)
#define REG_GP_SRC_ACTIVE_REGION_0_DIMENSIONS (0xA8)
#define REG_GP_SRC_ACTIVE_REGION_1_DIMENSIONS (0xAC)
#define REG_GP_SRC_ACTIVE_REGION_2_DIMENSIONS (0xB0)
#define REG_GP_SRC_ACTIVE_REGION_3_DIMENSIONS (0xB4)
#define REG_GP_CRC_DATA (0x160)
#define REG_GP_DMA_BANDWIDTH_RATE (0x170)
#define REG_GP_STATUS (0x184)
#define GP_STATUS_DECOMPRESSION_FAIL BIT(0)

#define GP_BLOCK_BASE_FOR(i) (GP_BLOCK_BASE + ((i) * GP_BLOCK_SIZE))
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
#define BLEND_LAYER_CONFIG_MODE(v) (((v) >> 4) & 0xF)
#define BLEND_MODE_NONE (0)
#define BLEND_MODE_ALPHA (1)
#define BLEND_MODE_PREMULT (2)
#define BLEND_MODE_BYPASS (3)
#define REG_BLEND_DEGAMMA_TABLE_R (0x1C)
#define REG_BLEND_DEGAMMA_TABLE_G (0x1024)
#define REG_BLEND_DEGAMMA_TABLE_B (0x202C)
// #define REG_BLEND_?? (0x3034)
#define REG_BLEND_PIXCAP_CONFIG (0x303C)

static void adp_v4_update_irqs(AppleDisplayPipeV4State *s)
{
    qemu_set_irq(s->irqs[0], (s->int_enable & s->int_status) != 0);
}

static pixman_format_code_t adp_v4_gp_fmt_to_pixman(ADPV4GenPipeState *s)
{
    if ((s->pixel_format & GP_PIXEL_FORMAT_BGRA) == GP_PIXEL_FORMAT_BGRA) {
        ADP_INFO("gp%d: pixel format is BGRA (0x%X).", s->index,
                 s->pixel_format);
        return PIXMAN_b8g8r8a8;
    }
    if ((s->pixel_format & GP_PIXEL_FORMAT_ARGB) == GP_PIXEL_FORMAT_ARGB) {
        ADP_INFO("gp%d: pixel format is ARGB (0x%X).", s->index,
                 s->pixel_format);
        return PIXMAN_a8r8g8b8;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "gp%d: pixel format is unknown (0x%X).\n",
                  s->index, s->pixel_format);
    return 0;
}

static void adp_v4_gp_read(ADPV4GenPipeState *s, AddressSpace *dma_as)
{
    uint32_t buf_len;

    s->buf_len = 0;

    // TODO: Decompress the data and display it properly.
    if (s->pixel_format & GP_PIXEL_FORMAT_COMPRESSED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gp%d: dropping frame as it's compressed.\n", s->index);
        return;
    }

    ADP_INFO("gp%d: width and height is %dx%d.", s->index, s->src_width,
             s->src_height);
    ADP_INFO("gp%d: stride is %d.", s->index, s->stride);

    buf_len = s->src_height * s->stride;
    if (s->max_buf_len < buf_len) {
        g_free(s->buf);
        s->buf = g_malloc(buf_len);
        s->max_buf_len = buf_len;
    }

    if (dma_memory_read(dma_as, s->data_start, s->buf, buf_len,
                        MEMTXATTRS_UNSPECIFIED) == MEMTX_OK) {
        s->buf_len = buf_len;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "gp%d: failed to read from DMA.\n",
                      s->index);
    }
}

static void adp_v4_gp_reg_write(ADPV4GenPipeState *s, hwaddr addr,
                                uint64_t data)
{
    switch (addr) {
    case REG_GP_CONFIG_CONTROL: {
        ADP_INFO("gp%d: control <- 0x" HWADDR_FMT_plx, s->index, data);
        s->config_control = (uint32_t)data;
        break;
    }
    case REG_GP_PIXEL_FORMAT: {
        ADP_INFO("gp%d: pixel format <- 0x" HWADDR_FMT_plx, s->index, data);
        s->pixel_format = (uint32_t)data;
        break;
    }
    case REG_GP_LAYER_0_DATA_START: {
        ADP_INFO("gp%d: layer 0 data start <- 0x" HWADDR_FMT_plx, s->index,
                 data);
        s->data_start = (uint32_t)data;
        break;
    }
    case REG_GP_LAYER_0_DATA_END: {
        ADP_INFO("gp%d: layer 0 data end <- 0x" HWADDR_FMT_plx, s->index, data);
        s->data_end = (uint32_t)data;
        break;
    }
    case REG_GP_LAYER_0_STRIDE: {
        ADP_INFO("gp%d: layer 0 stride <- 0x" HWADDR_FMT_plx, s->index, data);
        s->stride = (uint32_t)data;
        break;
    }
    case REG_GP_LAYER_0_DIMENSIONS: {
        s->src_height = data & 0xFFFF;
        s->src_width = (data >> 16) & 0xFFFF;
        ADP_INFO("gp%d: layer 0 dimensions <- 0x" HWADDR_FMT_plx " (%dx%d)",
                 s->index, data, s->src_width, s->src_height);
        break;
    }
    case REG_GP_DEST_DIMENSIONS: {
        s->dest_height = data & 0xFFFF;
        s->dest_width = (data >> 16) & 0xFFFF;
        ADP_INFO("gp%d: dest dimensions <- 0x" HWADDR_FMT_plx " (%dx%d)",
                 s->index, data, s->dest_width, s->dest_height);
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
        ADP_INFO("gp%d: control -> 0x%X", s->index, s->config_control);
        return s->config_control;
    }
    case REG_GP_PIXEL_FORMAT: {
        ADP_INFO("gp%d: pixel format -> 0x%X", s->index, s->pixel_format);
        return s->pixel_format;
    }
    case REG_GP_LAYER_0_DATA_START: {
        ADP_INFO("gp%d: layer 0 data start -> 0x%X", s->index, s->data_start);
        return s->data_start;
    }
    case REG_GP_LAYER_0_DATA_END: {
        ADP_INFO("gp%d: layer 0 data end -> 0x%X", s->index, s->data_end);
        return s->data_end;
    }
    case REG_GP_LAYER_0_STRIDE: {
        ADP_INFO("gp%d: layer 0 stride -> 0x%X", s->index, s->stride);
        return s->stride;
    }
    case REG_GP_LAYER_0_DIMENSIONS: {
        ADP_INFO("gp%d: layer 0 dimensions -> 0x%X (%dx%d)", s->index,
                 (s->src_width << 16) | s->src_height, s->src_width,
                 s->src_height);
        return ((uint32_t)s->src_width << 16) | s->src_height;
    }
    case REG_GP_DEST_DIMENSIONS: {
        ADP_INFO("gp%d: dest dimensions -> 0x%X (%dx%d)", s->index,
                 (s->dest_width << 16) | s->dest_height, s->dest_width,
                 s->dest_height);
        return ((uint32_t)s->dest_width << 16) | s->dest_height;
    }
    default: {
        ADP_INFO("gp%d: unknown @ 0x" HWADDR_FMT_plx " -> 0x" HWADDR_FMT_plx,
                 s->index, addr, (hwaddr)0);
        return 0;
    }
    }
}

static void adp_v4_gp_reset(ADPV4GenPipeState *s, uint8_t index)
{
    g_free(s->buf);
    memset(s, 0, sizeof(*s));
    s->index = index;
}

static void adp_v4_blend_reg_write(ADPV4BlendUnitState *s, uint64_t addr,
                                   uint64_t data)
{
    switch (addr) {
    case REG_BLEND_LAYER_0_CONFIG: {
        ADP_INFO("blend: layer 0 config <- 0x" HWADDR_FMT_plx, data);
        s->layer_config[0] = (uint32_t)data;
        break;
    }
    case REG_BLEND_LAYER_1_CONFIG: {
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

static void adp_v4_reg_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    AppleDisplayPipeV4State *s = opaque;

    QEMU_LOCK_GUARD(&s->lock);

    switch (addr >= 0x200000 ? addr - 0x200000 : addr) {
    case REG_CONTROL_INT_STATUS: {
        ADP_INFO("disp: int status <- 0x%X", (uint32_t)data);
        s->int_status &= ~(uint32_t)data;
        adp_v4_update_irqs(s);
        break;
    }
    case REG_CONTROL_INT_ENABLE: {
        ADP_INFO("disp: int enable <- 0x%X", (uint32_t)data);
        s->int_enable = (uint32_t)data;
        adp_v4_update_irqs(s);
        break;
    }
    case 0x4602C: {
        ADP_INFO("disp: REG_0x4602C <- 0x%X", (uint32_t)data);
        if (data & BIT32(12)) {
            qemu_bh_schedule(s->update_disp_image_bh);
        } else {
            qemu_bh_cancel(s->update_disp_image_bh);
        }
        break;
    }
    case GP_BLOCK_BASE_FOR(0)... GP_BLOCK_END_FOR(0): {
        adp_v4_gp_reg_write(&s->genpipe[0], addr - GP_BLOCK_BASE_FOR(0), data);
        break;
    }
    case GP_BLOCK_BASE_FOR(1)... GP_BLOCK_END_FOR(1): {
        adp_v4_gp_reg_write(&s->genpipe[1], addr - GP_BLOCK_BASE_FOR(1), data);
        break;
    }
    case BLEND_BLOCK_BASE ...(BLEND_BLOCK_BASE + BLEND_BLOCK_SIZE): {
        adp_v4_blend_reg_write(&s->blend_unit, addr - BLEND_BLOCK_BASE, data);
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
        ADP_INFO("disp: version -> 0x%X", CONTROL_VERSION_A1);
        return CONTROL_VERSION_A1;
    }
    case REG_CONTROL_FRAME_SIZE: {
        ADP_INFO("disp: frame size -> 0x%X", (s->width << 16) | s->height);
        return (s->width << 16) | s->height;
    }
    case REG_CONTROL_INT_STATUS: {
        ADP_INFO("disp: int status -> 0x%X", s->int_status);
        return s->int_status;
    }
    case GP_BLOCK_BASE_FOR(0)... GP_BLOCK_END_FOR(0): {
        return adp_v4_gp_reg_read(&s->genpipe[0], addr - GP_BLOCK_BASE_FOR(0));
    }
    case GP_BLOCK_BASE_FOR(1)... GP_BLOCK_END_FOR(1): {
        return adp_v4_gp_reg_read(&s->genpipe[1], addr - GP_BLOCK_BASE_FOR(1));
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
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void adp_v4_invalidate(void *opaque)
{
}

static void adp_v4_gfx_update(void *opaque)
{
    AppleDisplayPipeV4State *s = opaque;
    DirtyBitmapSnapshot *snap;
    bool dirty;
    uint32_t y, ys;

    snap = memory_region_snapshot_and_clear_dirty(
        s->vram_mr, s->vram_off + s->fb_off,
        s->height * s->width * sizeof(uint32_t), DIRTY_MEMORY_VGA);
    ys = -1U;
    for (y = 0; y < s->height; ++y) {
        dirty = memory_region_snapshot_get_dirty(
            s->vram_mr, snap,
            s->vram_off + s->fb_off + s->width * sizeof(uint32_t) * y,
            s->width * sizeof(uint32_t));
        if (dirty && ys == -1U) {
            ys = y;
        }
        if (!dirty && ys != -1U) {
            dpy_gfx_update(s->console, 0, ys, s->width, y - ys);
            ys = -1U;
        }
    }
    if (ys != -1U) {
        dpy_gfx_update(s->console, 0, ys, s->width, y - ys);
    }

    g_free(snap);

    s->int_status |= CONTROL_INT_OUTPUT_READY;
    adp_v4_update_irqs(s);
}

static const GraphicHwOps adp_v4_ops = {
    .invalidate = adp_v4_invalidate,
    .gfx_update = adp_v4_gfx_update,
};

static void *adp_v4_get_fb_ptr(AppleDisplayPipeV4State *s)
{
    return memory_region_get_ram_ptr(s->vram_mr) + s->vram_off + s->fb_off;
}

static void adp_v4_update_disp_image_ptr(AppleDisplayPipeV4State *s)
{
    pixman_image_t *image;

    image = pixman_image_create_bits(PIXMAN_a8r8g8b8, s->width, s->height,
                                     adp_v4_get_fb_ptr(s),
                                     s->width * sizeof(uint32_t));

    dpy_gfx_replace_surface(s->console,
                            qemu_create_displaysurface_pixman(image));
    qemu_pixman_image_unref(image);
}

typedef struct {
    AppleDisplayPipeV4State *s;
    uint32_t width;
    uint32_t height;
    pixman_transform_t transform;
    double dest_width;
    int16_t dest_x;
    int16_t dest_y;
    pixman_image_t *image;
    pixman_image_t *disp_image;
} ADPV4DrawBootSplashContext;

static void adp_v4_draw_boot_splash(void *opaque)
{
    ADPV4DrawBootSplashContext *ctx = opaque;

    pixman_image_composite(PIXMAN_OP_SRC, ctx->image, NULL, ctx->disp_image, 0,
                           0, 0, 0, ctx->dest_x, ctx->dest_y, ctx->dest_width,
                           ctx->dest_width);

    dpy_gfx_update_full(ctx->s->console);
}

static void adp_v4_draw_boot_splash_timer(void *opaque)
{
    ADPV4DrawBootSplashContext *ctx = opaque;

    QEMU_LOCK_GUARD(&ctx->s->lock);

    adp_v4_draw_boot_splash(ctx);

    pixman_image_unref(ctx->image);
    timer_free(ctx->s->boot_splash_timer);
    ctx->s->boot_splash_timer = NULL;
    g_free(ctx);
}

// Please see `ui/icons/CKBrandingNotice.md`
static void adp_v4_read_and_draw_boot_splash(AppleDisplayPipeV4State *s)
{
    char *path;
    FILE *fp;
    uint8_t sig[8] = { 0 };
    png_structp png_ptr;
    png_infop info_ptr;
    ADPV4DrawBootSplashContext *ctx;
    uint32_t *data;
    png_bytep *row_ptrs;
    uint32_t disp_width;
    uint32_t disp_height;

    path = get_relocated_path(CONFIG_QEMU_ICONDIR
                              "/hicolor/512x512/apps/CKQEMUBootSplash@2x.png");
    g_assert_nonnull(path);
    fp = fopen(path, "rb");
    if (fp == NULL) {
        error_setg(&error_abort, "Missing emulator branding: %s.", path);
        return;
    }
    fread(sig, sizeof(sig), 1, fp);
    if (png_sig_cmp(sig, 0, sizeof(sig)) != 0) {
        error_setg(&error_abort, "Invalid emulator branding: %s.", path);
        return;
    }
    g_free(path);
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    g_assert_nonnull(png_ptr);
    info_ptr = png_create_info_struct(png_ptr);
    g_assert_nonnull(info_ptr);
    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sizeof(sig));

    png_read_info(png_ptr, info_ptr);

    ctx = g_new(ADPV4DrawBootSplashContext, 1);
    ctx->width = png_get_image_width(png_ptr, info_ptr);
    ctx->height = png_get_image_height(png_ptr, info_ptr);
    ctx->image =
        pixman_image_create_bits(PIXMAN_a8b8g8r8, ctx->width, ctx->height, NULL,
                                 ctx->width * sizeof(uint32_t));
    data = pixman_image_get_data(ctx->image);

    png_read_update_info(png_ptr, info_ptr);

    row_ptrs = g_new(png_bytep, ctx->height);
    for (size_t y = 0; y < ctx->height; y++) {
        row_ptrs[y] = (png_bytep)(data + (y * ctx->width));
    }

    png_read_image(png_ptr, row_ptrs);

    g_free(row_ptrs);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    disp_width = qemu_console_get_width(s->console, 0);
    disp_height = qemu_console_get_height(s->console, 0);

    ctx->s = s;
    ctx->dest_width = (double)disp_width / 1.5;
    ctx->dest_x = (disp_width / 2) - (ctx->dest_width / 2);
    ctx->dest_y = (disp_height / 2) - (ctx->dest_width / 2);
    ctx->disp_image = qemu_console_surface(s->console)->image;

    pixman_image_set_filter(ctx->image, PIXMAN_FILTER_BEST, NULL, 0);
    pixman_transform_init_identity(&ctx->transform);
    pixman_transform_scale(
        &ctx->transform, NULL,
        pixman_double_to_fixed((double)ctx->width / ctx->dest_width),
        pixman_double_to_fixed((double)ctx->height / ctx->dest_width));
    pixman_image_set_transform(ctx->image, &ctx->transform);

    pixman_rectangle16_t rect = {
        .x = 0,
        .y = 0,
        .width = disp_width,
        .height = disp_height,
    };
    pixman_color_t color = QEMU_PIXMAN_COLOR_BLACK;

    pixman_image_fill_rectangles(PIXMAN_OP_SRC, ctx->disp_image, &color, 1,
                                 &rect);

    adp_v4_draw_boot_splash(ctx);

    s->boot_splash_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, adp_v4_draw_boot_splash_timer, ctx);

    // Workaround for `-v` removing the boot splash.
    timer_mod(s->boot_splash_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                        (NANOSECONDS_PER_SECOND / 2));
}

static void adp_v4_reset_hold(Object *obj, ResetType type)
{
    AppleDisplayPipeV4State *s = APPLE_DISPLAY_PIPE_V4(obj);

    QEMU_LOCK_GUARD(&s->lock);

    s->int_status = 0;
    s->int_enable = 0;

    adp_v4_update_irqs(s);

    adp_v4_update_disp_image_ptr(s);

    adp_v4_gp_reset(&s->genpipe[0], 0);
    adp_v4_gp_reset(&s->genpipe[1], 1);
    adp_v4_blend_reset(&s->blend_unit);

    adp_v4_read_and_draw_boot_splash(s);
}

static void adp_v4_realize(DeviceState *dev, Error **errp)
{
    AppleDisplayPipeV4State *s = APPLE_DISPLAY_PIPE_V4(dev);

    QEMU_LOCK_GUARD(&s->lock);

    s->console = graphic_console_init(dev, 0, &adp_v4_ops, s);
}

static const Property adp_v4_props[] = {
    DEFINE_PROP_UINT32("width", AppleDisplayPipeV4State, width, 0),
    DEFINE_PROP_UINT32("height", AppleDisplayPipeV4State, height, 0),
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

// TODO: handle source/dest position, etc.
static void adp_v4_gp_draw(ADPV4GenPipeState *genpipe, AddressSpace *dma_as,
                           pixman_image_t *disp_image, QemuConsole *console)
{
    pixman_format_code_t fmt;
    pixman_image_t *image;

    if ((genpipe->config_control & GP_CONFIG_CONTROL_RUN) == 0 ||
        (genpipe->config_control & GP_CONFIG_CONTROL_ENABLED) == 0) {
        return;
    }

    adp_v4_gp_read(genpipe, dma_as);

    if (genpipe->buf_len != 0) {
        fmt = adp_v4_gp_fmt_to_pixman(genpipe);
        image = pixman_image_create_bits(
            fmt, genpipe->src_width, genpipe->src_height,
            (uint32_t *)genpipe->buf, genpipe->stride);

        pixman_image_composite(PIXMAN_OP_SRC, image, NULL, disp_image, 0, 0, 0,
                               0, 0, 0, genpipe->dest_width,
                               genpipe->dest_height);
        pixman_image_unref(image);

        dpy_gfx_update(console, 0, 0, genpipe->dest_width,
                       genpipe->dest_height);
    }
}

static void adp_v4_update_disp_bh(void *opaque)
{
    AppleDisplayPipeV4State *s = opaque;
    pixman_image_t *disp_image;

    QEMU_LOCK_GUARD(&s->lock);

    disp_image = qemu_console_surface(s->console)->image;

    adp_v4_gp_draw(&s->genpipe[0], &s->dma_as, disp_image, s->console);
    adp_v4_gp_draw(&s->genpipe[1], &s->dma_as, disp_image, s->console);

    s->int_status |= CONTROL_INT_FRAME_PROCESSED;
    adp_v4_update_irqs(s);
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
        aio_bh_new_guarded(qemu_get_aio_context(), adp_v4_update_disp_bh, s,
                           &dev->mem_reentrancy_guard);

    apple_dt_set_prop_str(node, "display-target", "DisplayTarget5");
    apple_dt_set_prop(node, "display-timing-info", sizeof(adp_v4_timing_info),
                      adp_v4_timing_info);
    apple_dt_set_prop_u32(node, "bics-param-set", 0xD);
    apple_dt_set_prop_u32(node, "dot-pitch", 326);
    apple_dt_set_prop_null(node, "function-brightness_update");

    s->dma_mr = dma_mr;
    object_property_add_const_link(OBJECT(sbd), "dma_mr", OBJECT(s->dma_mr));
    address_space_init(&s->dma_as, s->dma_mr, "disp0.dma");

    prop = apple_dt_get_prop(node, "reg");
    g_assert_nonnull(prop);
    reg = (uint64_t *)prop->data;
    memory_region_init_io(&s->up_regs, OBJECT(sbd), &adp_v4_reg_ops, sbd,
                          "up.regs", reg[1]);
    sysbus_init_mmio(sbd, &s->up_regs);
    object_property_add_const_link(OBJECT(sbd), "up.regs", OBJECT(&s->up_regs));

    for (i = 0; i < ARRAY_SIZE(s->irqs); i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }

    return sbd;
}

void adp_v4_update_vram_mapping(AppleDisplayPipeV4State *s, MemoryRegion *mr,
                                hwaddr base, uint64_t size)
{
    s->vram_mr = mr;
    s->vram_off = base;
    s->vram_size = size;
    // Put framebuffer at the end of VRAM (the start is used for GP stuff).
    s->fb_off = s->vram_size - (s->height * s->width * sizeof(uint32_t));
}

uint64_t adp_v4_get_fb_off(AppleDisplayPipeV4State *s)
{
    return s->fb_off;
}
