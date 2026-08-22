#pragma once

#include "hw/arm/apple-silicon/dt.h"
#include "hw/spmi/spmi.h"
#include "qom/object.h"

#define TYPE_APPLE_SPMI_BASEBAND "apple-spmi-baseband"
OBJECT_DECLARE_SIMPLE_TYPE(AppleSPMIBasebandState, APPLE_SPMI_BASEBAND)

struct AppleSPMIBasebandState
{
    /*< private >*/
    SPMISlave parent_obj;

    /*< public >*/
    qemu_irq irq;
    uint8_t  reg[0xFFFF];
    uint16_t addr;
};

void         apple_spmi_baseband_set_irq(AppleSPMIBasebandState* s, int value);
DeviceState* apple_spmi_baseband_create(AppleDTNode* node);
