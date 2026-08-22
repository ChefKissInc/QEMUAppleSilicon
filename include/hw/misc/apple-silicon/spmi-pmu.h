#pragma once

#include "hw/arm/apple-silicon/dt.h"

DeviceState *apple_spmi_pmu_from_node(AppleDTNode *node);
