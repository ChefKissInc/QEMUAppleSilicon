/*
 * QEMU KVM stub
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Author: Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "system/kvm.h"
#include "hw/pci/msi.h"

KVMState* kvm_state;
bool      kvm_async_interrupts_allowed;
bool      kvm_resamplefds_allowed;
bool      kvm_gsi_routing_allowed;
bool      kvm_gsi_direct_mapping;
bool      kvm_allowed;
bool      kvm_readonly_mem_allowed;
bool      kvm_msi_use_devid;

void kvm_flush_coalesced_mmio_buffer(void) { }

bool kvm_has_sync_mmu(void) { return false; }

int kvm_on_sigbus_vcpu(CPUState* cpu, int code, void* addr) { return 1; }

int kvm_on_sigbus(int code, void* addr) { return 1; }

void kvm_init_irq_routing(KVMState* s) { }

unsigned int kvm_get_max_memslots(void) { return 0; }

unsigned int kvm_get_free_memslots(void) { return 0; }

bool kvm_arm_supports_user_irq(void) { return false; }

bool kvm_dirty_ring_enabled(void) { return false; }

uint32_t kvm_dirty_ring_size(void) { return 0; }

bool kvm_hwpoisoned_mem(void) { return false; }

int kvm_create_guest_memfd(uint64_t size, uint64_t flags, Error** errp) { return -ENOSYS; }
