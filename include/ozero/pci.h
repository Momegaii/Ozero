#ifndef O0_PCI_H
#define O0_PCI_H

#include <stdint.h>
#include <stddef.h>

struct o0_pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
};

#define O0_PCI_MAX_DEVICES 64u

size_t o0_pci_init(void);
size_t o0_pci_device_count(void);
const struct o0_pci_device *o0_pci_device(size_t index);
int o0_pci_find(uint16_t vendor_id, uint16_t device_id,
                struct o0_pci_device *out);

#endif