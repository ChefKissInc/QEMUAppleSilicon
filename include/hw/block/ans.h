#pragma once

#include "hw/arm/dt.h"
#include "hw/misc/a7iop/base.h"
#include "hw/pci/pci.h"
#include "hw/sysbus.h"

SysBusDevice* apple_ans_from_node(AppleDTNode* node, AppleA7IOPVersion version, PCIBus* pci_bus);
