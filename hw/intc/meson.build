system_ss.add(files('intc.c'))
system_ss.add(when: 'CONFIG_ARM_GIC', if_true: files(
  'arm_gic.c',
  'arm_gic_common.c',
  'arm_gicv2m.c',
  'arm_gicv3_common.c',
  'arm_gicv3_its_common.c',
))
system_ss.add(when: 'CONFIG_ARM_GICV3', if_true: files(
  'arm_gicv3.c',
  'arm_gicv3_dist.c',
  'arm_gicv3_its.c',
  'arm_gicv3_redist.c',
))
specific_ss.add(when: 'CONFIG_APPLE_SOC', if_true: files('apple_aic.c'))
arm_common_ss.add(when: 'CONFIG_ARM_GIC', if_true: files('arm_gicv3_cpuif_common.c'))
arm_common_ss.add(when: 'CONFIG_ARM_GICV3', if_true: files('arm_gicv3_cpuif.c'))
specific_ss.add(when: 'CONFIG_ARM_GIC_KVM', if_true: files('arm_gic_kvm.c'))
specific_ss.add(when: ['CONFIG_ARM_GIC_KVM', 'TARGET_AARCH64'], if_true: files('arm_gicv3_kvm.c', 'arm_gicv3_its_kvm.c'))
