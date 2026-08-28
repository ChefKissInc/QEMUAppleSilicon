pci_ss = ss.source_set()
pci_ss.add(when: 'CONFIG_PCIE_PORT', if_true: files('pcie_root_port.c'))
system_ss.add_all(when: 'CONFIG_PCI', if_true: pci_ss)
