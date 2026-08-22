/* Poison identifiers that should not be used when building
   target independent device code.  */

#pragma once

#include "config-poison.h"

#pragma GCC poison TARGET_AARCH64
#pragma GCC poison TARGET_ARM
#pragma GCC poison TARGET_ABI32

#pragma GCC poison TARGET_HAS_BFLT
#pragma GCC poison TARGET_NAME
#pragma GCC poison TARGET_BIG_ENDIAN
#pragma GCC poison TCG_GUEST_DEFAULT_MO

#pragma GCC poison TARGET_LONG_BITS
#pragma GCC poison TARGET_FMT_lx
#pragma GCC poison TARGET_FMT_ld
#pragma GCC poison TARGET_FMT_lu

#pragma GCC poison TARGET_PHYS_ADDR_SPACE_BITS

#pragma GCC poison CONFIG_I386_DIS

#pragma GCC poison CONFIG_HVF
#pragma GCC poison CONFIG_KVM
#pragma GCC poison CONFIG_WHPX
#pragma GCC poison CONFIG_XEN

#pragma GCC poison CONFIG_USER_ONLY
#pragma GCC poison COMPILING_SYSTEM_VS_USER
#pragma GCC poison tcg_use_softmmu
#pragma GCC poison CONFIG_LINUX_USER
#pragma GCC poison CONFIG_BSD_USER

#pragma GCC poison KVM_HAVE_MCE_INJECTION
