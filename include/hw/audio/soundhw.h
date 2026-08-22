#pragma once

void pci_register_soundhw(const char* name, const char* descr, int (*init_pci)(PCIBus* bus, const char* audiodev));

void soundhw_init(void);
void show_valid_soundhw(void);
void select_soundhw(const char* name, const char* audiodev);
