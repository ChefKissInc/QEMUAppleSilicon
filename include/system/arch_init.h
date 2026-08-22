#pragma once

enum {
    QEMU_ARCH_ALL = -1,
    QEMU_ARCH_ARM = (1 << 1),
    QEMU_ARCH_I386 = (1 << 0),
};

bool qemu_arch_available(unsigned qemu_arch_mask);
