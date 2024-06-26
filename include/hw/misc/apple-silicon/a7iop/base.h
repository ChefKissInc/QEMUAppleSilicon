#ifndef HW_MISC_APPLE_SILICON_A7IOP_BASE_H
#define HW_MISC_APPLE_SILICON_A7IOP_BASE_H

#include "qemu/osdep.h"

#define APPLE_A7IOP_IOP_IRQ "apple-a7iop-iop-irq"

typedef enum {
    APPLE_A7IOP_V2 = 0,
    APPLE_A7IOP_V4,
} AppleA7IOPVersion;

enum {
    APPLE_A7IOP_IRQ_IOP_NONEMPTY = 0,
    APPLE_A7IOP_IRQ_IOP_EMPTY,
    APPLE_A7IOP_IRQ_AP_NONEMPTY,
    APPLE_A7IOP_IRQ_AP_EMPTY,
    APPLE_A7IOP_IRQ_MAX,
};

#endif /* HW_MISC_APPLE_SILICON_A7IOP_BASE_H */
