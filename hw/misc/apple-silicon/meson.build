system_ss.add(when: 'CONFIG_APPLE_SOC', if_true: files(
    'aes.c',
    'a7iop/core.c',
    'a7iop/mailbox/core.c',
    'a7iop/mailbox/regs-v2.c',
    'a7iop/mailbox/regs-v4.c',
    'a7iop/regs-v2.c',
    'a7iop/regs-v4.c',
    'a7iop/rtkit.c',
    'smc.c',
    'roswell.c',
    'chestnut.c',
    'pmu-d2255.c',
    'aop.c',
    'baseband.c',
    'buttons.c',
    'tempsensor.c',
    'mic_tempsensor.c',
))
system_ss.add(when: 'CONFIG_APPLE_SPMI_PMU', if_true: files('spmi-pmu.c'))
system_ss.add(when: 'CONFIG_APPLE_SPMI_BASEBAND', if_true: files('spmi-baseband.c'))

