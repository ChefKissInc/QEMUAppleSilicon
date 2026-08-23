arm_ss = ss.source_set()
arm_common_ss = ss.source_set()

arm_common_ss.add(when: 'CONFIG_APPLE_SOC', if_true: tasn1)
arm_common_ss.add(when: 'CONFIG_APPLE_DART', if_true: files('dart.c'),
                                             if_false: files('dart-stub.c'))
arm_common_ss.add(when: 'CONFIG_APPLE_SART', if_true: files('sart.c'))
arm_common_ss.add(when: 'CONFIG_APPLE_SOC', if_true: files(
    'a13.c',
    'a13_gxf.c',
    'a9.c',
    'boot.c',
    'dt.c',
    'mem.c',
    'sep-sim.c',
    'sep.c',
    'patcher.c',
    'kernel_patches.c',
    'lm-backlight.c',
    's8000.c',
    't8030.c',
))

hw_arch += {'arm': arm_ss}
hw_common_arch += {'arm': arm_common_ss}
