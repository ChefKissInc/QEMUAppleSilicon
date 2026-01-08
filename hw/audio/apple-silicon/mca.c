/*
 * Apple Multi-Channel Audio Controller.
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

// MMIO Index 0: SmartIO MCA
#define SIO_MCA_REG_STRIDE (0x4000)

// SmartIO MCA Units
// 0x000: Clock Generator
// 0x100: Sync Generator
// 0x200: RX0
// 0x300: TX0
// 0x400: RX1
// 0x500: TX1
// 0x600: Master Clock Pin
// 0x700: Interrupts
#define SIO_MCA_UNIT_REG_STRIDE (0x100)

#define SIO_UNIT_CTL_ENABLE BIT(0)
#define SIO_UNIT_CTL_RESET BIT(1)

#define REG_SIO_UNIT_CTL (0x0)

#define REG_PIN_CLK_SEL (0x4)
#define MCLK_SEL_CFG_I2S_CLOCK_MASK (0x7)
#define MCLK_SEL_CFG_I2S_CLOCK(v) ((v) & MCLK_PIN_CFG_I2S_CLOCK_SEL_MASK)
#define REG_PIN_DATA_SEL (0x8)

#define REG_INT_STS (0x700)
#define REG_INT_MASK (0x704)

// MMIO Index 1: MCA DMA
#define MCA_DMA_REG_STRIDE (0x4000)

// MMIO Index 2: Master Clock Config
#define MCLK_CFG_REG_STRIDE (0x4)

#define REG_MCLK_CFG (0x0)
#define MCLK_CFG_ENABLED BIT(31)
