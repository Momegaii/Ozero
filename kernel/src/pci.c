#include <stdint.h>
#include <stddef.h>

#include "ozero/pci.h"
#include "ozero/status.h"

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}
static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

static struct o0_pci_device pci_devices[O0_PCI_MAX_DEVICES];
static size_t pci_device_count = 0;

static uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func,
                                uint8_t offset) {
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)dev << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

size_t o0_pci_init(void) {
    pci_device_count = 0;
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_config_read((uint8_t)bus, dev, func, 0);
                uint16_t vendor = (uint16_t)(id & 0xFFFFu);
                if (vendor == 0xFFFFu) {
                    if (func == 0) break;
                    continue;
                }
                if (pci_device_count >= O0_PCI_MAX_DEVICES) return pci_device_count;
                uint16_t device = (uint16_t)((id >> 16) & 0xFFFFu);
                uint32_t classrev = pci_config_read((uint8_t)bus, dev, func, 8);
                struct o0_pci_device *d = &pci_devices[pci_device_count++];
                d->bus = (uint8_t)bus;
                d->device = dev;
                d->function = func;
                d->vendor_id = vendor;
                d->device_id = device;
                d->revision = (uint8_t)(classrev & 0xFFu);
                d->prog_if = (uint8_t)((classrev >> 8) & 0xFFu);
                d->subclass = (uint8_t)((classrev >> 16) & 0xFFu);
                d->class_code = (uint8_t)((classrev >> 24) & 0xFFu);
                if (func == 0) {
                    uint32_t header = pci_config_read((uint8_t)bus, dev, func, 12);
                    if (!((header >> 16) & 0x80u)) break;
                }
            }
        }
    }
    return pci_device_count;
}

size_t o0_pci_device_count(void) { return pci_device_count; }
const struct o0_pci_device *o0_pci_device(size_t index) {
    if (index >= pci_device_count) return NULL;
    return &pci_devices[index];
}
int o0_pci_find(uint16_t vendor_id, uint16_t device_id, struct o0_pci_device *out) {
    for (size_t i = 0; i < pci_device_count; ++i) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id) {
            if (out) *out = pci_devices[i];
            return 0;
        }
    }
    return -1;
}