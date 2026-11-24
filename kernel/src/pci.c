#include <stdbool.h>
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

static volatile uint32_t pci_lock = 0;
static void pci_lock_acquire(void) { while (__atomic_test_and_set(&pci_lock, __ATOMIC_ACQUIRE)) {} }
static void pci_lock_release(void) { __atomic_clear(&pci_lock, __ATOMIC_RELEASE); }

static struct o0_pci_device pci_devices[O0_PCI_MAX_DEVICES];
static size_t pci_device_count = 0;
static bool pci_truncated = false;

static uint32_t pci_config_address(struct o0_pci_bdf bdf, uint8_t offset) {
    return (1u << 31) |
           ((uint32_t)bdf.bus << 16) |
           ((uint32_t)bdf.device << 11) |
           ((uint32_t)bdf.function << 8) |
           (offset & 0xFCu);
}

static int valid_offset(uint8_t offset, uint8_t width) {
    if (offset > 0xFCu) return -1;
    if (width == 1) return 0;
    if (width == 2) return (offset % 2u) == 0;
    if (width == 4) return (offset % 4u) == 0;
    return -1;
}

int o0_pci_config_read32(struct o0_pci_bdf bdf, uint8_t offset, uint32_t *out) {
    if (!out || !valid_offset(offset,4)) return -1;
    pci_lock_acquire();
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bdf, offset));
    *out = inl(PCI_CONFIG_DATA);
    pci_lock_release();
    return 0;
}
int o0_pci_config_read16(struct o0_pci_bdf bdf, uint8_t offset, uint16_t *out) {
    uint32_t v; int r;
    if (!out || !valid_offset(offset,2)) return -1;
    r = o0_pci_config_read32(bdf, offset & ~3u, &v);
    if (r != 0) return r;
    *out = (uint16_t)((v >> ((offset % 4u)*8u)) & 0xFFFFu);
    return 0;
}
int o0_pci_config_read8(struct o0_pci_bdf bdf, uint8_t offset, uint8_t *out) {
    uint32_t v; int r;
    if (!out || !valid_offset(offset,1)) return -1;
    r = o0_pci_config_read32(bdf, offset & ~3u, &v);
    if (r != 0) return r;
    *out = (uint8_t)((v >> ((offset % 4u)*8u)) & 0xFFu);
    return 0;
}
int o0_pci_config_write32(struct o0_pci_bdf bdf, uint8_t offset, uint32_t value) {
    if (!valid_offset(offset,4)) return -1;
    pci_lock_acquire();
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bdf, offset));
    outl(PCI_CONFIG_DATA, value);
    pci_lock_release();
    return 0;
}
int o0_pci_config_write16(struct o0_pci_bdf bdf, uint8_t offset, uint16_t value) {
    uint32_t v; int r;
    if (!valid_offset(offset,2)) return -1;
    r = o0_pci_config_read32(bdf, offset & ~3u, &v);
    if (r != 0) return r;
    uint8_t shift = (offset % 4u)*8u;
    v = (v & ~(0xFFFFu << shift)) | ((uint32_t)value << shift);
    return o0_pci_config_write32(bdf, offset & ~3u, v);
}
int o0_pci_config_write8(struct o0_pci_bdf bdf, uint8_t offset, uint8_t value) {
    uint32_t v; int r;
    if (!valid_offset(offset,1)) return -1;
    r = o0_pci_config_read32(bdf, offset & ~3u, &v);
    if (r != 0) return r;
    uint8_t shift = (offset % 4u)*8u;
    v = (v & ~(0xFFu << shift)) | ((uint32_t)value << shift);
    return o0_pci_config_write32(bdf, offset & ~3u, v);
}

int o0_pci_decode_bar(uint32_t raw0, uint32_t raw1, uint8_t header_type,
                      uint8_t index, struct o0_pci_bar *out) {
    if (!out) return -1;
    out->index = index;
    out->header_type = header_type;
    out->is_memory = !(raw0 & 1u);
    out->prefetchable = out->is_memory && ((raw0 >> 3) & 1u);
    out->is_64bit = out->is_memory && ((raw0 >> 1) & 3u) == 2u;
    out->base = out->is_64bit ? ((uint64_t)raw1 << 32) | (raw0 & ~0xFu) : (raw0 & ~0xFu);
    out->assigned = out->base != 0;
    out->valid = true;
    out->size = 0;
    out->alignment = 0;
    return 0;
}

int o0_pci_probe_bar(struct o0_pci_bdf bdf, uint8_t index,
                     uint8_t header_type, struct o0_pci_bar *out) {
    if (!out) return -1;
    if (index > 5) return -1;
    uint8_t off = 0x10u + index*4u;
    if (index > 0 && /* 64-bit previous handled by caller */ 0) {}
    uint32_t orig0, orig1=0, cmd;
    int r;
    r = o0_pci_config_read32(bdf, off, &orig0); if (r) return r;
    if ((orig0 & 0x6u) == 0x4u) { /* 64-bit memory */
        r = o0_pci_config_read32(bdf, off+4, &orig1); if (r) return r;
    }
    r = o0_pci_config_read16(bdf, 0x04u, (uint16_t*)&cmd); if (r) return r;
    /* disable decode */
    o0_pci_config_write16(bdf, 0x04u, (uint16_t)(cmd & ~0x3u));
    /* write all ones */
    o0_pci_config_write32(bdf, off, 0xFFFFFFFFu);
    if ((orig0 & 0x6u) == 0x4u) o0_pci_config_write32(bdf, off+4, 0xFFFFFFFFu);
    uint32_t size0, size1=0;
    r = o0_pci_config_read32(bdf, off, &size0); if (r) { /* restore */ }
    if ((orig0 & 0x6u) == 0x4u) r = o0_pci_config_read32(bdf, off+4, &size1);
    /* restore */
    o0_pci_config_write32(bdf, off, orig0);
    if ((orig0 & 0x6u) == 0x4u) o0_pci_config_write32(bdf, off+4, orig1);
    o0_pci_config_write16(bdf, 0x04u, (uint16_t)cmd);
    /* decode */
    uint32_t raw1 = ((orig0 & 0x6u)==0x4u) ? orig1 : 0;
    o0_pci_decode_bar(orig0, raw1, 0, index, out);
    /* compute size */
    if (out->is_memory) {
        uint64_t mask = out->is_64bit ? (((uint64_t)size1 << 32) | size0) : size0;
        mask &= ~0xFu;
        if (mask == 0) out->size = 0;
        else out->size = (~mask) + 1u;
    }
    out->header_type = header_type;
    return 0;
}

int o0_pci_validate_mmio(uint64_t base, uint64_t size) {
    if (size == 0) return 0;
    if (base + size < base) return -1; /* overflow */
    /* placeholder: accept; real validation against Ozero physical resource model */
    return 0;
}

int o0_pci_find_capabilities(struct o0_pci_bdf bdf, struct o0_pci_resources *res) {
    if (!res) return -1;
    uint8_t cap_ptr; uint8_t status;
    int r = o0_pci_config_read8(bdf, 0x06u, &status); /* not exact; placeholder */
    (void)status;
    r = o0_pci_config_read8(bdf, 0x34u, &cap_ptr);
    if (r) return r;
    while (cap_ptr != 0 && cap_ptr >= 0x40u) {
        uint8_t cap_id;
        r = o0_pci_config_read8(bdf, cap_ptr, &cap_id); if (r) return r;
        if (cap_id == 0x05u) { res->msi_supported = true; res->msi_cap_offset = cap_ptr; }
        if (cap_id == 0x11u) {
            res->msix_supported = true; res->msix_cap_offset = cap_ptr;
            uint32_t t; r = o0_pci_config_read32(bdf, cap_ptr+4, &t); if (!r) {
                res->msix_table_offset = (uint16_t)(t & 0xFFF8u);
                res->msix_table_bir = (uint8_t)(t & 0x7u);
            }
            r = o0_pci_config_read32(bdf, cap_ptr+8, &t); if (!r) {
                res->msix_pba_offset = (uint16_t)(t & 0xFFF8u);
                res->msix_pba_bir = (uint8_t)(t & 0x7u);
            }
        }
        uint8_t next; r = o0_pci_config_read8(bdf, cap_ptr+1, &next); if (r) return r;
        cap_ptr = next;
    }
    return 0;
}

size_t o0_pci_init(void) {
    pci_device_count = 0;
    pci_truncated = false;
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                struct o0_pci_bdf bdf = { (uint8_t)bus, dev, func };
                uint32_t id; int r = o0_pci_config_read32(bdf, 0, &id);
                if (r != 0) continue;
                uint16_t vendor = (uint16_t)(id & 0xFFFFu);
                if (vendor == 0xFFFFu) {
                    if (func == 0) break;
                    continue;
                }
                if (pci_device_count >= O0_PCI_MAX_DEVICES) { pci_truncated = true; /* skip */ 
                    if (func == 0) {
                        uint32_t hdr; o0_pci_config_read32(bdf,0x0C,&hdr);
                        if (!((hdr>>16)&0x80u)) break;
                    }
                    continue;
                }
                uint16_t device = (uint16_t)((id >> 16) & 0xFFFFu);
                uint32_t classrev; o0_pci_config_read32(bdf, 8, &classrev);
                struct o0_pci_device *d = &pci_devices[pci_device_count++];
                d->bdf = bdf;
                d->vendor_id = vendor;
                d->device_id = device;
                d->revision = (uint8_t)(classrev & 0xFFu);
                d->prog_if = (uint8_t)((classrev >> 8) & 0xFFu);
                d->subclass = (uint8_t)((classrev >> 16) & 0xFFu);
                d->class_code = (uint8_t)((classrev >> 24) & 0xFFu);
                uint32_t hdr; o0_pci_config_read32(bdf, 0x0C, &hdr);
                d->resources.header_type = (uint8_t)((hdr >> 16) & 0xFFu);
                /* bar count by header type */
                if (d->resources.header_type == 0) d->resources.bar_count = 6;
                else if (d->resources.header_type == 1) d->resources.bar_count = 2;
                else d->resources.bar_count = 0;
                d->resources.interrupt_pin = 0;
                uint8_t pin; if (o0_pci_config_read8(bdf, 0x3Du, &pin)==0) d->resources.interrupt_pin = pin;
                o0_pci_find_capabilities(bdf, &d->resources);
                /* dma constraints unknown by default */
                d->dma.platform_known = false;
                d->dma.device_known = false;
                d->dma.coherent_known = false;
                d->truncated = false;
                if (func == 0) {
                    if (!((hdr >> 16) & 0x80u)) break;
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