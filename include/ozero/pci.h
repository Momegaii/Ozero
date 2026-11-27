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
    uint16_t msix_table_size; /* number of table entries, from the control word */
    bool msi_supported;
    bool msix_supported;
    bool msi_64bit;           /* MSI message address is 64-bit */
    bool msi_per_vector_mask;
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
bool o0_pci_truncated(void);
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

/* Converts the all-ones size mask read back from a BAR into a byte size. Pure, so
 * host tests can check it against recorded config-space dumps. */
uint64_t o0_pci_bar_size_from_mask(bool is_memory, bool is_64bit,
                                   uint32_t mask_low, uint32_t mask_high);
int o0_pci_probe_bar(struct o0_pci_bdf bdf, uint8_t index,
                     uint8_t header_type, struct o0_pci_bar *out);
int o0_pci_validate_mmio(uint64_t base, uint64_t size);

/* Capabilities */
int o0_pci_find_capabilities(struct o0_pci_bdf bdf, struct o0_pci_resources *res);

/* Pure parser for a 256-byte config-space image. Walks the capability list only when
 * status bit 4 (capabilities list) is set, bounds every offset, caps the walk length and
 * records the MSI (0x05) and MSI-X (0x11) capabilities. Host tests feed it recorded
 * dumps; the kernel feeds it a snapshot read through o0_pci_config_read8. */
int o0_pci_parse_capabilities(const uint8_t *cfg, size_t len,
                              struct o0_pci_resources *res);

/* ---- MSI / MSI-X ---- */

/* One MSI message as written into the capability: destination APIC ID in the address,
 * vector and delivery mode in the data word. Fixed delivery, physical destination. */
uint64_t o0_pci_msi_address(uint32_t dest_apic_id);
uint32_t o0_pci_msi_data(uint32_t vector, bool level_assert);

/* MSI-X table entry (16 bytes on the wire, little-endian). */
struct o0_pci_msix_entry {
    uint64_t address;
    uint32_t data;
    uint32_t vector_control; /* bit 0: masked */
};

int o0_pci_msix_encode(uint32_t dest_apic_id, uint32_t vector, bool masked,
                       struct o0_pci_msix_entry *out);

/* Programs the MSI capability at cap_offset and enables it for one vector. The vector
 * must already be reserved with o0_irq_alloc_vectors. */
int o0_pci_msi_enable(struct o0_pci_bdf bdf, uint8_t cap_offset,
                      uint32_t dest_apic_id, uint32_t vector);
int o0_pci_msi_disable(struct o0_pci_bdf bdf, uint8_t cap_offset);

/* Writes one MSI-X table entry through a caller-provided mapping of the table BAR
 * (map it with o0_device_map_bar; the table is not reachable through the HHDM). The
 * index is bounds-checked against the capability's table size. */
int o0_pci_msix_program(volatile void *table, uint16_t table_entries, uint16_t index,
                        uint32_t dest_apic_id, uint32_t vector, bool masked);

/* Programs one entry, then clears the function mask and enables MSI-X. */
int o0_pci_msix_enable(struct o0_pci_bdf bdf, uint8_t cap_offset,
                       volatile void *table, uint16_t index,
                       uint32_t dest_apic_id, uint32_t vector);
int o0_pci_msix_disable(struct o0_pci_bdf bdf, uint8_t cap_offset);

#endif