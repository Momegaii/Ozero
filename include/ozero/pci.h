#ifndef O0_PCI_H
#define O0_PCI_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct o0_pci_bdf {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

struct o0_pci_bar {
    uint8_t index;
    uint8_t header_type;
    bool is_memory;
    bool prefetchable;
    bool is_64bit;
    uint64_t base;
    uint64_t size;
    uint64_t alignment;
    bool assigned;
    bool valid;
};

struct o0_pci_resources {
    uint8_t header_type;
    uint8_t bar_count;
    struct o0_pci_bar bars[6];
    uint8_t interrupt_pin;
    uint8_t msi_cap_offset;
    uint8_t msix_cap_offset;
    uint16_t msix_table_offset;
    uint8_t msix_table_bir;
    uint16_t msix_pba_offset;
    uint8_t msix_pba_bir;
    bool msi_supported;
    bool msix_supported;
};

struct o0_pci_dma_constraints {
    bool platform_known;
    uint8_t platform_address_width;
    uint32_t platform_alignment;
    bool device_known;
    uint8_t device_address_width;
    uint32_t device_max_segment;
    bool coherent_known;
    bool coherent;
};

struct o0_pci_device {
    struct o0_pci_bdf bdf;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    struct o0_pci_resources resources;
    struct o0_pci_dma_constraints dma;
    bool truncated;
};

#define O0_PCI_MAX_DEVICES 64u

size_t o0_pci_init(void);
size_t o0_pci_device_count(void);
const struct o0_pci_device *o0_pci_device(size_t index);
int o0_pci_find(uint16_t vendor_id, uint16_t device_id, struct o0_pci_device *out);

/* Configuration access */
int o0_pci_config_read8(struct o0_pci_bdf bdf, uint8_t offset, uint8_t *out);
int o0_pci_config_read16(struct o0_pci_bdf bdf, uint8_t offset, uint16_t *out);
int o0_pci_config_read32(struct o0_pci_bdf bdf, uint8_t offset, uint32_t *out);
int o0_pci_config_write8(struct o0_pci_bdf bdf, uint8_t offset, uint8_t value);
int o0_pci_config_write16(struct o0_pci_bdf bdf, uint8_t offset, uint16_t value);
int o0_pci_config_write32(struct o0_pci_bdf bdf, uint8_t offset, uint32_t value);

/* BAR decode/probe */
int o0_pci_decode_bar(uint32_t raw0, uint32_t raw1, uint8_t header_type,
                      uint8_t index, struct o0_pci_bar *out);
int o0_pci_probe_bar(struct o0_pci_bdf bdf, uint8_t index,
                     uint8_t header_type, struct o0_pci_bar *out);
int o0_pci_validate_mmio(uint64_t base, uint64_t size);

/* Capabilities */
int o0_pci_find_capabilities(struct o0_pci_bdf bdf, struct o0_pci_resources *res);

#endif