system_ss.add(when: 'CONFIG_UNIMP', if_true: files('unimp.c'))
system_ss.add(when: 'CONFIG_EMPTY_SLOT', if_true: files('empty_slot.c'))
system_ss.add(when: 'CONFIG_LED', if_true: files('led.c'))
system_ss.add(when: 'CONFIG_AUX', if_true: files('auxbus.c'))
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
    'fan53740.c',
))
system_ss.add(when: 'CONFIG_APPLE_SPMI_PMU', if_true: files('spmi-pmu.c'))
system_ss.add(when: 'CONFIG_APPLE_SPMI_BASEBAND', if_true: files('spmi-baseband.c'))

