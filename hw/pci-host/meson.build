pci_ss = ss.source_set()
pci_ss.add(when: 'CONFIG_APPLE_SOC', if_true: files('apcie.c'))
system_ss.add_all(when: 'CONFIG_PCI', if_true: pci_ss)
