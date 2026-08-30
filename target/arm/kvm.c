/*
 * ARM implementation of KVM hooks
 *
 * Copyright Christoffer Dall 2009-2010
 * Copyright Mian-M. Hamayun 2013, Virtual Open Systems
 * Copyright Alex Bennée 2014, Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>

#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "system/system.h"
#include "system/runstate.h"
#include "system/kvm.h"
#include "system/kvm_int.h"
#include "kvm_arm.h"
#include "cpu.h"
#include "cpu-sysregs.h"
#include "trace.h"
#include "internals.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "system/address-spaces.h"
#include "gdbstub/enums.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "target/arm/gtimer.h"

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {KVM_CAP_INFO(DEVICE_CTRL), KVM_CAP_LAST_INFO};

static bool cap_has_mp_state;
static bool cap_has_inject_serror_esr;
static bool cap_has_inject_ext_dabt;

/**
 * ARMHostCPUFeatures: information about the host CPU (identified
 * by asking the host kernel)
 */
typedef struct ARMHostCPUFeatures
{
    ARMISARegisters isar;
    uint64_t        features;
    uint32_t        target;
    const char*     dtb_compatible;
} ARMHostCPUFeatures;

static ARMHostCPUFeatures arm_host_cpu_features;

/**
 * kvm_arm_vcpu_init:
 * @cpu: ARMCPU
 *
 * Initialize (or reinitialize) the VCPU by invoking the
 * KVM_ARM_VCPU_INIT ioctl with the CPU type and feature
 * bitmask specified in the CPUState.
 *
 * Returns: 0 if success else < 0 error code
 */
static int kvm_arm_vcpu_init(ARMCPU* cpu)
{
    struct kvm_vcpu_init init;

    init.target = cpu->kvm_target;
    memcpy(init.features, cpu->kvm_init_features, sizeof(init.features));

    return kvm_vcpu_ioctl(CPU(cpu), KVM_ARM_VCPU_INIT, &init);
}

/**
 * kvm_arm_vcpu_finalize:
 * @cpu: ARMCPU
 * @feature: feature to finalize
 *
 * Finalizes the configuration of the specified VCPU feature by
 * invoking the KVM_ARM_VCPU_FINALIZE ioctl. Features requiring
 * this are documented in the "KVM_ARM_VCPU_FINALIZE" section of
 * KVM's API documentation.
 *
 * Returns: 0 if success else < 0 error code
 */
static int kvm_arm_vcpu_finalize(ARMCPU* cpu, int feature)
{ return kvm_vcpu_ioctl(CPU(cpu), KVM_ARM_VCPU_FINALIZE, &feature); }

bool kvm_arm_create_scratch_host_vcpu(int* fdarray, struct kvm_vcpu_init* init)
{
    int ret = 0, kvmfd = -1, vmfd = -1, cpufd = -1;
    int max_vm_pa_size;

    kvmfd = qemu_open_old("/dev/kvm", O_RDWR);
    if (kvmfd < 0) { goto err; }
    max_vm_pa_size = ioctl(kvmfd, KVM_CHECK_EXTENSION, KVM_CAP_ARM_VM_IPA_SIZE);
    if (max_vm_pa_size < 0) { max_vm_pa_size = 0; }
    do {
        vmfd = ioctl(kvmfd, KVM_CREATE_VM, max_vm_pa_size);
    }
    while (vmfd == -1 && errno == EINTR);
    if (vmfd < 0) { goto err; }

    /*
     * The MTE capability must be enabled by the VMM before creating
     * any VCPUs in order to allow the MTE bits of the ID_AA64PFR1
     * register to be probed correctly, as they are masked if MTE
     * is not enabled.
     */
    if (kvm_arm_mte_supported()) {
        KVMState kvm_state;

        kvm_state.fd   = kvmfd;
        kvm_state.vmfd = vmfd;
        kvm_vm_enable_cap(&kvm_state, KVM_CAP_ARM_MTE, 0);
    }

    cpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
    if (cpufd < 0) { goto err; }

    if (!init) {
        /* Caller doesn't want the VCPU to be initialized, so skip it */
        goto finish;
    }

    if (init->target == -1) {
        struct kvm_vcpu_init preferred;

        ret = ioctl(vmfd, KVM_ARM_PREFERRED_TARGET, &preferred);
        if (ret < 0) { goto err; }
        init->target = preferred.target;
    }
    ret = ioctl(cpufd, KVM_ARM_VCPU_INIT, init);
    if (ret < 0) { goto err; }

finish:
    fdarray[0] = kvmfd;
    fdarray[1] = vmfd;
    fdarray[2] = cpufd;

    return true;

err:
    if (cpufd >= 0) { close(cpufd); }
    if (vmfd >= 0) { close(vmfd); }
    if (kvmfd >= 0) { close(kvmfd); }

    return false;
}

void kvm_arm_destroy_scratch_host_vcpu(int* fdarray)
{
    int i;

    for (i = 2; i >= 0; i--) { close(fdarray[i]); }
}

static int read_sys_reg32(int fd, uint32_t* pret, uint64_t id)
{
    uint64_t           ret;
    struct kvm_one_reg idreg = {.id = id, .addr = (uintptr_t)&ret};
    int                err;

    assert((id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U64);
    err = ioctl(fd, KVM_GET_ONE_REG, &idreg);
    if (err < 0) { return -1; }
    *pret = ret;
    return 0;
}

static int read_sys_reg64(int fd, uint64_t* pret, uint64_t id)
{
    struct kvm_one_reg idreg = {.id = id, .addr = (uintptr_t)pret};

    assert((id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U64);
    return ioctl(fd, KVM_GET_ONE_REG, &idreg);
}

static bool kvm_arm_pauth_supported(void)
{
    return (kvm_check_extension(kvm_state, KVM_CAP_ARM_PTRAUTH_ADDRESS)
            && kvm_check_extension(kvm_state, KVM_CAP_ARM_PTRAUTH_GENERIC));
}

static uint64_t idregs_sysreg_to_kvm_reg(ARMSysRegs sysreg)
{
    return ARM64_SYS_REG((sysreg & CP_REG_ARM64_SYSREG_OP0_MASK) >> CP_REG_ARM64_SYSREG_OP0_SHIFT,
                         (sysreg & CP_REG_ARM64_SYSREG_OP1_MASK) >> CP_REG_ARM64_SYSREG_OP1_SHIFT,
                         (sysreg & CP_REG_ARM64_SYSREG_CRN_MASK) >> CP_REG_ARM64_SYSREG_CRN_SHIFT,
                         (sysreg & CP_REG_ARM64_SYSREG_CRM_MASK) >> CP_REG_ARM64_SYSREG_CRM_SHIFT,
                         (sysreg & CP_REG_ARM64_SYSREG_OP2_MASK) >> CP_REG_ARM64_SYSREG_OP2_SHIFT);
}

/* read a sysreg value and store it in the idregs */
static int get_host_cpu_reg(int fd, ARMHostCPUFeatures* ahcf, ARMIDRegisterIdx index)
{
    uint64_t* reg;
    int       ret;

    reg = &ahcf->isar.idregs[index];
    ret = read_sys_reg64(fd, reg, idregs_sysreg_to_kvm_reg(id_register_sysreg[index]));
    return ret;
}

static bool kvm_arm_get_host_cpu_features(ARMHostCPUFeatures* ahcf)
{
    /* Identify the feature bits corresponding to the host CPU, and
     * fill out the ARMHostCPUClass fields accordingly. To do this
     * we have to create a scratch VM, create a single CPU inside it,
     * and then query that CPU for the relevant ID registers.
     */
    int      fdarray[3];
    bool     sve_supported;
    bool     el2_supported;
    bool     pmu_supported = false;
    uint64_t features      = 0;
    int      err;

    /*
     * target = -1 informs kvm_arm_create_scratch_host_vcpu()
     * to use the preferred target
     */
    struct kvm_vcpu_init init = {
        .target = -1,
    };

    /*
     * Ask for SVE if supported, so that we can query ID_AA64ZFR0,
     * which is otherwise RAZ.
     */
    sve_supported = kvm_arm_sve_supported();
    if (sve_supported) { init.features[0] |= 1 << KVM_ARM_VCPU_SVE; }

    /*
     * Ask for EL2 if supported.
     */
    el2_supported = kvm_arm_el2_supported();
    if (el2_supported) { init.features[0] |= 1 << KVM_ARM_VCPU_HAS_EL2; }

    /*
     * Ask for Pointer Authentication if supported, so that we get
     * the unsanitized field values for AA64ISAR1_EL1.
     */
    if (kvm_arm_pauth_supported()) {
        init.features[0] |= (1 << KVM_ARM_VCPU_PTRAUTH_ADDRESS | 1 << KVM_ARM_VCPU_PTRAUTH_GENERIC);
    }

    if (kvm_arm_pmu_supported()) {
        init.features[0] |= 1 << KVM_ARM_VCPU_PMU_V3;
        pmu_supported     = true;
        features         |= 1ULL << ARM_FEATURE_PMU;
    }

    if (!kvm_arm_create_scratch_host_vcpu(fdarray, &init)) { return false; }

    ahcf->target         = init.target;
    ahcf->dtb_compatible = "arm,armv8";
    int fd               = fdarray[2];

    err = get_host_cpu_reg(fd, ahcf, ID_AA64PFR0_EL1_IDX);
    if (unlikely(err < 0)) {
        /*
         * Before v4.15, the kernel only exposed a limited number of system
         * registers, not including any of the interesting AArch64 ID regs.
         * For the most part we could leave these fields as zero with minimal
         * effect, since this does not affect the values seen by the guest.
         *
         * However, it could cause problems down the line for QEMU,
         * so provide a minimal v8.0 default.
         *
         * ??? Could read MIDR and use knowledge from cpu64.c.
         * ??? Could map a page of memory into our temp guest and
         *     run the tiniest of hand-crafted kernels to extract
         *     the values seen by the guest.
         * ??? Either of these sounds like too much effort just
         *     to work around running a modern host kernel.
         */
        SET_IDREG(&ahcf->isar, ID_AA64PFR0, 0x00000011); /* EL1&0, AArch64 only */
        err = 0;
    }
    else {
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64PFR1_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64SMFR0_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64DFR0_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64DFR1_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64ISAR0_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64ISAR1_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64ISAR2_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64MMFR0_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64MMFR1_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64MMFR2_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_AA64MMFR3_EL1_IDX);

        /*
         * Note that if AArch32 support is not present in the host,
         * the AArch32 sysregs are present to be read, but will
         * return UNKNOWN values.  This is neither better nor worse
         * than skipping the reads and leaving 0, as we must avoid
         * considering the values in every case.
         */
        err |= get_host_cpu_reg(fd, ahcf, ID_PFR0_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_PFR1_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_DFR0_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_MMFR0_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_MMFR1_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_MMFR2_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_MMFR3_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_ISAR0_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_ISAR1_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_ISAR2_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_ISAR3_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_ISAR4_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_ISAR5_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_ISAR6_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_MMFR4_EL1_IDX);

        err |= read_sys_reg32(fd, &ahcf->isar.mvfr0, ARM64_SYS_REG(3, 0, 0, 3, 0));
        err |= read_sys_reg32(fd, &ahcf->isar.mvfr1, ARM64_SYS_REG(3, 0, 0, 3, 1));
        err |= read_sys_reg32(fd, &ahcf->isar.mvfr2, ARM64_SYS_REG(3, 0, 0, 3, 2));
        err |= get_host_cpu_reg(fd, ahcf, ID_PFR2_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_DFR1_EL1_IDX);
        err |= get_host_cpu_reg(fd, ahcf, ID_MMFR5_EL1_IDX);

        /*
         * DBGDIDR is a bit complicated because the kernel doesn't
         * provide an accessor for it in 64-bit mode, which is what this
         * scratch VM is in, and there's no architected "64-bit sysreg
         * which reads the same as the 32-bit register" the way there is
         * for other ID registers. Instead we synthesize a value from the
         * AArch64 ID_AA64DFR0, the same way the kernel code in
         * arch/arm64/kvm/sys_regs.c:trap_dbgidr() does.
         * We only do this if the CPU supports AArch32 at EL1.
         */
        if (FIELD_EX32_IDREG(&ahcf->isar, ID_AA64PFR0, EL1) >= 2) {
            int      wrps     = FIELD_EX64_IDREG(&ahcf->isar, ID_AA64DFR0, WRPS);
            int      brps     = FIELD_EX64_IDREG(&ahcf->isar, ID_AA64DFR0, BRPS);
            int      ctx_cmps = FIELD_EX64_IDREG(&ahcf->isar, ID_AA64DFR0, CTX_CMPS);
            int      version  = 6; /* ARMv8 debug architecture */
            bool     has_el3  = !!FIELD_EX32_IDREG(&ahcf->isar, ID_AA64PFR0, EL3);
            uint32_t dbgdidr  = 0;

            dbgdidr             = REG_FIELD_DP32(dbgdidr, DBGDIDR, WRPS, wrps);
            dbgdidr             = REG_FIELD_DP32(dbgdidr, DBGDIDR, BRPS, brps);
            dbgdidr             = REG_FIELD_DP32(dbgdidr, DBGDIDR, CTX_CMPS, ctx_cmps);
            dbgdidr             = REG_FIELD_DP32(dbgdidr, DBGDIDR, VERSION, version);
            dbgdidr             = REG_FIELD_DP32(dbgdidr, DBGDIDR, NSUHD_IMP, has_el3);
            dbgdidr             = REG_FIELD_DP32(dbgdidr, DBGDIDR, SE_IMP, has_el3);
            dbgdidr            |= (1 << 15); /* RES1 bit */
            ahcf->isar.dbgdidr  = dbgdidr;
        }

        if (pmu_supported) {
            /* PMCR_EL0 is only accessible if the vCPU has feature PMU_V3 */
            err |= read_sys_reg64(fd, &ahcf->isar.reset_pmcr_el0, ARM64_SYS_REG(3, 3, 9, 12, 0));
        }

        if (sve_supported) {
            /*
             * There is a range of kernels between kernel commit 73433762fcae
             * and f81cb2c3ad41 which have a bug where the kernel doesn't
             * expose SYS_ID_AA64ZFR0_EL1 via the ONE_REG API unless the VM has
             * enabled SVE support, which resulted in an error rather than RAZ.
             * So only read the register if we set KVM_ARM_VCPU_SVE above.
             */
            err |= get_host_cpu_reg(fd, ahcf, ID_AA64ZFR0_EL1_IDX);
        }
    }

    kvm_arm_destroy_scratch_host_vcpu(fdarray);

    if (err < 0) { return false; }

    /*
     * We can assume any KVM supporting CPU is at least a v8
     * with VFPv4+Neon; this in turn implies most of the other
     * feature bits.
     */
    features |= 1ULL << ARM_FEATURE_V8;
    features |= 1ULL << ARM_FEATURE_NEON;
    features |= 1ULL << ARM_FEATURE_AARCH64;
    features |= 1ULL << ARM_FEATURE_GENERIC_TIMER;

    if (el2_supported) { features |= 1ULL << ARM_FEATURE_EL2; }

    ahcf->features = features;

    return true;
}

void kvm_arm_set_cpu_features_from_host(ARMCPU* cpu)
{
    CPUARMState* env = &cpu->env;

    if (!arm_host_cpu_features.dtb_compatible) {
        if (!kvm_enabled() || !kvm_arm_get_host_cpu_features(&arm_host_cpu_features)) {
            /* We can't report this error yet, so flag that we need to
             * in arm_cpu_realizefn().
             */
            cpu->kvm_target            = QEMU_KVM_ARM_TARGET_NONE;
            cpu->host_cpu_probe_failed = true;
            return;
        }
    }

    cpu->kvm_target     = arm_host_cpu_features.target;
    cpu->dtb_compatible = arm_host_cpu_features.dtb_compatible;
    cpu->isar           = arm_host_cpu_features.isar;
    env->features       = arm_host_cpu_features.features;
}

static bool kvm_no_adjvtime_get(Object* obj, Error** errp) { return !ARM_CPU(obj)->kvm_adjvtime; }

static void kvm_no_adjvtime_set(Object* obj, bool value, Error** errp) { ARM_CPU(obj)->kvm_adjvtime = !value; }

static bool kvm_steal_time_get(Object* obj, Error** errp) { return ARM_CPU(obj)->kvm_steal_time != ON_OFF_AUTO_OFF; }

static void kvm_steal_time_set(Object* obj, bool value, Error** errp)
{ ARM_CPU(obj)->kvm_steal_time = value ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF; }

/* KVM VCPU properties should be prefixed with "kvm-". */
void kvm_arm_add_vcpu_properties(ARMCPU* cpu)
{
    CPUARMState* env = &cpu->env;
    Object*      obj = OBJECT(cpu);

    if (arm_feature(env, ARM_FEATURE_GENERIC_TIMER)) {
        cpu->kvm_adjvtime = true;
        object_property_add_bool(obj, "kvm-no-adjvtime", kvm_no_adjvtime_get, kvm_no_adjvtime_set);
        object_property_set_description(obj, "kvm-no-adjvtime",
                                        "Set on to disable the adjustment of "
                                        "the virtual counter. VM stopped time "
                                        "will be counted.");
    }

    cpu->kvm_steal_time = ON_OFF_AUTO_AUTO;
    object_property_add_bool(obj, "kvm-steal-time", kvm_steal_time_get, kvm_steal_time_set);
    object_property_set_description(obj, "kvm-steal-time", "Set off to disable KVM steal time.");
}

bool kvm_arm_pmu_supported(void) { return kvm_check_extension(kvm_state, KVM_CAP_ARM_PMU_V3); }

int kvm_arm_get_max_vm_ipa_size(MachineState* ms, bool* fixed_ipa)
{
    KVMState* s = KVM_STATE(ms->accelerator);
    int       ret;

    ret        = kvm_check_extension(s, KVM_CAP_ARM_VM_IPA_SIZE);
    *fixed_ipa = ret <= 0;

    return ret > 0 ? ret : 40;
}

int kvm_arch_get_default_type(MachineState* ms)
{
    bool fixed_ipa;
    int  size = kvm_arm_get_max_vm_ipa_size(ms, &fixed_ipa);
    return fixed_ipa ? 0 : size;
}

int kvm_arch_init(MachineState* ms, KVMState* s)
{
    int ret = 0;
    /* For ARM interrupt delivery is always asynchronous,
     * the state of the CPU interrupt lines.
     */
    kvm_async_interrupts_allowed = true;

    /*
     * PSCI wakes up secondary cores, so we always need to
     * have vCPUs waiting in kernel space
     */
    kvm_halt_in_kernel_allowed = true;

    cap_has_mp_state = kvm_check_extension(s, KVM_CAP_MP_STATE);

    /* Check whether user space can specify guest syndrome value */
    cap_has_inject_serror_esr = kvm_check_extension(s, KVM_CAP_ARM_INJECT_SERROR_ESR);

    if (ms->smp.cpus > 256 && !kvm_check_extension(s, KVM_CAP_ARM_IRQ_LINE_LAYOUT_2)) {
        error_report("Using more than 256 vcpus requires a host kernel "
                     "with KVM_CAP_ARM_IRQ_LINE_LAYOUT_2");
        ret = -EINVAL;
    }

    if (kvm_check_extension(s, KVM_CAP_ARM_NISV_TO_USER)) {
        if (kvm_vm_enable_cap(s, KVM_CAP_ARM_NISV_TO_USER, 0)) {
            error_report("Failed to enable KVM_CAP_ARM_NISV_TO_USER cap");
        }
        else {
            /* Set status for supporting the external dabt injection */
            cap_has_inject_ext_dabt = kvm_check_extension(s, KVM_CAP_ARM_INJECT_EXT_DABT);
        }
    }

    if (s->kvm_eager_split_size) {
        uint32_t sizes;

        sizes = kvm_vm_check_extension(s, KVM_CAP_ARM_SUPPORTED_BLOCK_SIZES);
        if (!sizes) {
            s->kvm_eager_split_size = 0;
            warn_report("Eager Page Split support not available");
        }
        else if (!(s->kvm_eager_split_size & sizes)) {
            error_report("Eager Page Split requested chunk size not valid");
            ret = -EINVAL;
        }
        else {
            ret = kvm_vm_enable_cap(s, KVM_CAP_ARM_EAGER_SPLIT_CHUNK_SIZE, 0, s->kvm_eager_split_size);
            if (ret < 0) { error_report("Enabling of Eager Page Split failed: %s", strerror(-ret)); }
        }
    }

    max_hw_wps     = kvm_check_extension(s, KVM_CAP_GUEST_DEBUG_HW_WPS);
    hw_watchpoints = g_array_sized_new(true, true, sizeof(HWWatchpoint), max_hw_wps);

    max_hw_bps     = kvm_check_extension(s, KVM_CAP_GUEST_DEBUG_HW_BPS);
    hw_breakpoints = g_array_sized_new(true, true, sizeof(HWBreakpoint), max_hw_bps);

    return ret;
}

unsigned long kvm_arch_vcpu_id(CPUState* cpu) { return cpu->cpu_index; }

/* We track all the KVM devices which need their memory addresses
 * passing to the kernel in a list of these structures.
 * When board init is complete we run through the list and
 * tell the kernel the base addresses of the memory regions.
 * We use a MemoryListener to track mapping and unmapping of
 * the regions during board creation, so the board models don't
 * need to do anything special for the KVM case.
 *
 * Sometimes the address must be OR'ed with some other fields
 * (for example for KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION).
 * @kda_addr_ormask aims at storing the value of those fields.
 */
typedef struct KVMDevice
{
    struct kvm_arm_device_addr kda;
    struct kvm_device_attr     kdattr;
    uint64_t                   kda_addr_ormask;
    MemoryRegion*              mr;
    QSLIST_ENTRY(KVMDevice) entries;
    int dev_fd;
} KVMDevice;

static QSLIST_HEAD(, KVMDevice) kvm_devices_head;

static void kvm_arm_devlistener_add(MemoryListener* listener, MemoryRegionSection* section)
{
    KVMDevice* kd;

    QSLIST_FOREACH (kd, &kvm_devices_head, entries) {
        if (section->mr == kd->mr) { kd->kda.addr = section->offset_within_address_space; }
    }
}

static void kvm_arm_devlistener_del(MemoryListener* listener, MemoryRegionSection* section)
{
    KVMDevice* kd;

    QSLIST_FOREACH (kd, &kvm_devices_head, entries) {
        if (section->mr == kd->mr) { kd->kda.addr = -1; }
    }
}

static MemoryListener devlistener = {
    .name       = "kvm-arm",
    .region_add = kvm_arm_devlistener_add,
    .region_del = kvm_arm_devlistener_del,
    .priority   = MEMORY_LISTENER_PRIORITY_MIN,
};

static void kvm_arm_set_device_addr(KVMDevice* kd)
{
    struct kvm_device_attr* attr = &kd->kdattr;
    int                     ret;
    uint64_t                addr = kd->kda.addr;

    addr       |= kd->kda_addr_ormask;
    attr->addr  = (uintptr_t)&addr;
    ret         = kvm_device_ioctl(kd->dev_fd, KVM_SET_DEVICE_ATTR, attr);

    if (ret < 0) {
        fprintf(stderr, "Failed to set device address: %s\n", strerror(-ret));
        abort();
    }
}

int kvm_arm_set_irq(int cpu, int irqtype, int irq, int level)
{
    int kvm_irq  = (irqtype << KVM_ARM_IRQ_TYPE_SHIFT) | irq;
    int cpu_idx1 = cpu % 256;
    int cpu_idx2 = cpu / 256;

    kvm_irq |= (cpu_idx1 << KVM_ARM_IRQ_VCPU_SHIFT) | (cpu_idx2 << KVM_ARM_IRQ_VCPU2_SHIFT);

    return kvm_set_irq(kvm_state, kvm_irq, !!level);
}

void arm_cpu_kvm_set_irq(void* arm_cpu, int irq, int level)
{
    ARMCPU*      cpu = arm_cpu;
    CPUARMState* env = &cpu->env;
    CPUState*    cs  = CPU(cpu);
    uint32_t     linestate_bit;
    int          irq_id;

    switch (irq) {
        case ARM_CPU_IRQ:
            irq_id        = KVM_ARM_IRQ_CPU_IRQ;
            linestate_bit = CPU_INTERRUPT_HARD;
            break;
        case ARM_CPU_FIQ:
            irq_id        = KVM_ARM_IRQ_CPU_FIQ;
            linestate_bit = CPU_INTERRUPT_FIQ;
            break;
        default: assert_not_reached();
    }

    if (level) { qatomic_or(&env->irq_line_state, linestate_bit); }
    else {
        qatomic_and(&env->irq_line_state, ~linestate_bit);
    }

    kvm_arm_set_irq(cs->cpu_index, KVM_ARM_IRQ_TYPE_CPU, irq_id, !!level);
}

static void kvm_arm_machine_init_done(Notifier* notifier, void* data)
{
    KVMDevice *kd, *tkd;

    QSLIST_FOREACH_SAFE (kd, &kvm_devices_head, entries, tkd) {
        if (kd->kda.addr != -1) { kvm_arm_set_device_addr(kd); }
        memory_region_unref(kd->mr);
        QSLIST_REMOVE_HEAD(&kvm_devices_head, entries);
        g_free(kd);
    }
    memory_listener_unregister(&devlistener);
}

static Notifier notify = {
    .notify = kvm_arm_machine_init_done,
};
