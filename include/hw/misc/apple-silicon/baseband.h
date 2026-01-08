/*
 * Apple iPhone 11 Baseband
 *
 * Copyright (c) 2025-2026 Christian Inci (chris-pcguy).
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

#ifndef HW_MISC_APPLE_SILICON_BASEBAND_H
#define HW_MISC_APPLE_SILICON_BASEBAND_H

#include "hw/arm/apple-silicon/dt.h"
#include "hw/pci-host/apcie.h"
#include "hw/pci/pci.h"
#include "hw/sysbus.h"

#define BASEBAND_GPIO_COREDUMP "baseband-gpio-coredump"
#define BASEBAND_GPIO_RESET_DET_OUT "baseband-gpio-reset_det-out"

SysBusDevice *apple_baseband_create(AppleDTNode *node, PCIBus *pci_bus,
                                    ApplePCIEPort *port);

#endif /* HW_MISC_APPLE_SILICON_BASEBAND_H */
