#pragma once

int slotid_cap_init(PCIDevice *dev, int nslots,
                    uint8_t chassis,
                    unsigned offset,
                    Error **errp);
void slotid_cap_cleanup(PCIDevice *dev);
