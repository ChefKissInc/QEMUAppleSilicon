#include "qemu/osdep.h"
#include "exec/cpu-common.h"

RAMBlock* qemu_ram_block_from_host(void* ptr, bool round_offset, ram_addr_t* offset) { return NULL; }

int qemu_ram_get_fd(const RAMBlock* rb) { return -1; }
