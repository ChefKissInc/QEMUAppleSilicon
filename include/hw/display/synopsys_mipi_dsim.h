/*
 * Synopsys MIPI DSIM.
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

#ifndef HW_DISPLAY_SYNOPSYS_MIPI_DSIM_H
#define HW_DISPLAY_SYNOPSYS_MIPI_DSIM_H

#include "qemu/osdep.h"
#include "hw/arm/apple-silicon/dt.h"
#include "hw/sysbus.h"

#define TYPE_SYNOPSYS_MIPI_DSIM "synopsys-mipi-dsim"
OBJECT_DECLARE_SIMPLE_TYPE(SynopsysMIPIDSIMState, SYNOPSYS_MIPI_DSIM);

SysBusDevice *synopsys_mipi_dsim_create(AppleDTNode *node);

#endif /* HW_DISPLAY_SYNOPSYS_MIPI_DSIM_H */
