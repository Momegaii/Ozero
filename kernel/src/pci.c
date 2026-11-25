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

/* Locked accessors: the caller must already hold pci_lock. They let a multi-step
 * operation (such as a BAR size probe) take the lock once instead of re-entering the
 * non-recursive public wrappers, which would self-deadlock on the same lock. */
static int pci_read32_locked(struct o0_pci_bdf bdf, uint8_t offset, uint32_t *out) {
    if (!out || !valid_offset(offset, 4)) return -1;
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bdf, offset));
    *out = inl(PCI_CONFIG_DATA);
    return 0;
}
static int pci_read16_locked(struct o0_pci_bdf bdf, uint8_t offset, uint16_t *out) {
    uint32_t v; int r;
    if (!out || !valid_offset(offset, 2)) return -1;
    r = pci_read32_locked(bdf, offset & ~3u, &v);
    if (r != 0) return r;
    *out = (uint16_t)((v >> ((offset % 4u) * 8u)) & 0xFFFFu);
    return 0;
}
static int pci_read8_locked(struct o0_pci_bdf bdf, uint8_t offset, uint8_t *out) {
    uint32_t v; int r;
    if (!out || !valid_offset(offset, 1)) return -1;
    r = pci_read32_locked(bdf, offset & ~3u, &v);
    if (r != 0) return r;
    *out = (uint8_t)((v >> ((offset % 4u) * 8u)) & 0xFFu);
    return 0;
}
static int pci_write32_locked(struct o0_pci_bdf bdf, uint8_t offset, uint32_t value) {
    if (!valid_offset(offset, 4)) return -1;
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bdf, offset));
    outl(PCI_CONFIG_DATA, value);
    return 0;
}
static int pci_write16_locked(struct o0_pci_bdf bdf, uint8_t offset, uint16_t value) {
    uint32_t v; int r;
    if (!valid_offset(offset, 2)) return -1;
    r = pci_read32_locked(bdf, offset & ~3u, &v);
    if (r != 0) return r;
    uint8_t shift = (offset % 4u) * 8u;
    v = (v & ~(0xFFFFu << shift)) | ((uint32_t)value << shift);
    return pci_write32_locked(bdf, offset & ~3u, v);
}
static int pci_write8_locked(struct o0_pci_bdf bdf, uint8_t offset, uint8_t value) {
    uint32_t v; int r;
    if (!valid_offset(offset, 1)) return -1;
    r = pci_read32_locked(bdf, offset & ~3u, &v);
    if (r != 0) return r;
    uint8_t shift = (offset % 4u) * 8u;
    v = (v & ~(0xFFu << shift)) | ((uint32_t)value << shift);
    return pci_write32_locked(bdf, offset & ~3u, v);
}

int o0_pci_config_read32(struct o0_pci_bdf bdf, uint8_t offset, uint32_t *out) {
    pci_lock_acquire();
    int r = pci_read32_locked(bdf, offset, out);
    pci_lock_release();
    return r;
}
int o0_pci_config_read16(struct o0_pci_bdf bdf, uint8_t offset, uint16_t *out) {
    pci_lock_acquire();
    int r = pci_read16_locked(bdf, offset, out);
    pci_lock_release();
    return r;
}
int o0_pci_config_read8(struct o0_pci_bdf bdf, uint8_t offset, uint8_t *out) {
    pci_lock_acquire();
    int r = pci_read8_locked(bdf, offset, out);
    pci_lock_release();
    return r;
}
int o0_pci_config_write32(struct o0_pci_bdf bdf, uint8_t offset, uint32_t value) {
    pci_lock_acquire();
    int r = pci_write32_locked(bdf, offset, value);
    pci_lock_release();
    return r;
}
int o0_pci_config_write16(struct o0_pci_bdf bdf, uint8_t offset, uint16_t value) {
    pci_lock_acquire();
    int r = pci_write16_locked(bdf, offset, value);
    pci_lock_release();
    return r;
}
int o0_pci_config_write8(struct o0_pci_bdf bdf, uint8_t offset, uint8_t value) {
    pci_lock_acquire();
    int r = pci_write8_locked(bdf, offset, value);
    pci_lock_release();
    return r;
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

    pci_lock_acquire();
    uint32_t orig0=0, orig1=0, cmd=0;
    int r = 0;
    int restoration_failed = 0;
    uint32_t size0=0, size1=0;
    int is64 = 0;

    r = pci_read32_locked(bdf, off, &orig0);
    if (r == 0 && ((orig0 & 0x6u) == 0x4u)) {
        is64 = 1;
        r = pci_read32_locked(bdf, off+4, &orig1);
    }
    if (r == 0) r = pci_read16_locked(bdf, 0x04u, (uint16_t*)&cmd);

    if (r == 0) {
        /* disable decode */
        if (pci_write16_locked(bdf, 0x04u, (uint16_t)(cmd & ~0x3u)) != 0) r = -1;
    }
    if (r == 0) {
        if (pci_write32_locked(bdf, off, 0xFFFFFFFFu) != 0) r = -1;
        if (is64 && pci_write32_locked(bdf, off+4, 0xFFFFFFFFu) != 0) r = -1;
    }
    if (r == 0) {
        if (pci_read32_locked(bdf, off, &size0) != 0) r = -1;
        if (is64 && pci_read32_locked(bdf, off+4, &size1) != 0) r = -1;
    }
    /* restore always */
    if (pci_write32_locked(bdf, off, orig0) != 0) restoration_failed = 1;
    if (is64 && pci_write32_locked(bdf, off+4, orig1) != 0) restoration_failed = 1;
    if (pci_write16_locked(bdf, 0x04u, (uint16_t)cmd) != 0) restoration_failed = 1;

    pci_lock_release();

    if (r != 0) return r;
    if (restoration_failed) return -2;

    uint32_t raw1 = is64 ? orig1 : 0;
    o0_pci_decode_bar(orig0, raw1, header_type, index, out);
    out->size = o0_pci_bar_size_from_mask(out->is_memory, out->is_64bit,
                                          size0, is64 ? size1 : 0u);
    out->header_type = header_type;
    out->assigned = out->base != 0;
    return 0;
}

uint64_t o0_pci_bar_size_from_mask(bool is_memory, bool is_64bit,
                                   uint32_t mask_low, uint32_t mask_high) {
    if (!is_memory) {
        uint32_t mask = mask_low & ~0x3u;
        return mask == 0u ? 0u : (uint64_t)(~mask) + 1u;
    }
    if (is_64bit) {
        uint64_t mask = (((uint64_t)mask_high << 32) | mask_low) & ~(uint64_t)0xFu;
        return mask == 0u ? 0u : (~mask) + 1u;
    }
    /* 32-bit BAR: compute in 32 bits so ~mask does not flip the high half. */
    uint32_t mask = mask_low & ~0xFu;
    return mask == 0u ? 0u : (uint64_t)(~mask) + 1u;
}

int o0_pci_validate_mmio(uint64_t base, uint64_t size) {
    if (size == 0) return 0;
    if (base + size < base) return -1; /* overflow */
    /* placeholder: accept; real validation against Ozero physical resource model */
    return 0;
}

#define O0_PCI_CFG_BYTES 0x100u
#define O0_PCI_CAP_MAX_WALK 48u

int o0_pci_parse_capabilities(const uint8_t *cfg, size_t len,
                              struct o0_pci_resources *res) {
    if (cfg == NULL || res == NULL || len < O0_PCI_CFG_BYTES) {
        return -1;
    }
    /* Status register bit 4 says the capability list is present; without it the
     * pointer at 0x34 is reserved and must not be followed. */
    if ((cfg[0x06] & 0x10u) == 0u) {
        return 0;
    }

    uint8_t cap_ptr = cfg[0x34];
    for (uint32_t walk = 0; walk < O0_PCI_CAP_MAX_WALK; ++walk) {
        if (cap_ptr < 0x40u) {
            break; /* 0 terminates; below 0x40 is reserved/invalid */
        }
        if ((size_t)cap_ptr + 2u > len) {
            return -1; /* truncated list */
        }
        uint8_t cap_id = cfg[cap_ptr];
        uint8_t next = cfg[cap_ptr + 1u];

        if (cap_id == 0x05u) { /* MSI */
            if ((size_t)cap_ptr + 0x0Au > len) return -1;
            uint16_t ctrl = (uint16_t)(cfg[cap_ptr + 2u] | (cfg[cap_ptr + 3u] << 8));
            res->msi_supported = true;
            res->msi_cap_offset = cap_ptr;
            res->msi_64bit = (ctrl & 0x0080u) != 0u;
            res->msi_per_vector_mask = (ctrl & 0x0100u) != 0u;
        } else if (cap_id == 0x11u) { /* MSI-X */
            if ((size_t)cap_ptr + 0x0Cu > len) return -1;
            uint16_t ctrl = (uint16_t)(cfg[cap_ptr + 2u] | (cfg[cap_ptr + 3u] << 8));
            uint32_t table = (uint32_t)cfg[cap_ptr + 4u] |
                             ((uint32_t)cfg[cap_ptr + 5u] << 8) |
                             ((uint32_t)cfg[cap_ptr + 6u] << 16) |
                             ((uint32_t)cfg[cap_ptr + 7u] << 24);
            uint32_t pba = (uint32_t)cfg[cap_ptr + 8u] |
                           ((uint32_t)cfg[cap_ptr + 9u] << 8) |
                           ((uint32_t)cfg[cap_ptr + 10u] << 16) |
                           ((uint32_t)cfg[cap_ptr + 11u] << 24);
            uint8_t bir_table = (uint8_t)(table & 0x7u);
            uint8_t bir_pba = (uint8_t)(pba & 0x7u);
            if (bir_table < res->bar_count && bir_pba < res->bar_count) {
                res->msix_supported = true;
                res->msix_cap_offset = cap_ptr;
                res->msix_table_offset = (uint16_t)(table & 0xFFF8u);
                res->msix_table_bir = bir_table;
                res->msix_pba_offset = (uint16_t)(pba & 0xFFF8u);
                res->msix_pba_bir = bir_pba;
                res->msix_table_size = (uint16_t)((ctrl & 0x07FFu) + 1u);
            }
        }

        if (next == cap_ptr) {
            return -1; /* self-referential list */
        }
        cap_ptr = next;
    }
    return 0;
}

int o0_pci_find_capabilities(struct o0_pci_bdf bdf, struct o0_pci_resources *res) {
    if (res == NULL) {
        return -1;
    }
    uint8_t cfg[O0_PCI_CFG_BYTES];

    /* One snapshot under one lock so the list cannot change mid-walk. */
    pci_lock_acquire();
    for (uint32_t off = 0; off < O0_PCI_CFG_BYTES; off += 4u) {
        uint32_t value = 0;
        if (pci_read32_locked(bdf, (uint8_t)off, &value) != 0) {
            pci_lock_release();
            return -1;
        }
        cfg[off + 0u] = (uint8_t)(value & 0xFFu);
        cfg[off + 1u] = (uint8_t)((value >> 8) & 0xFFu);
        cfg[off + 2u] = (uint8_t)((value >> 16) & 0xFFu);
        cfg[off + 3u] = (uint8_t)((value >> 24) & 0xFFu);
    }
    pci_lock_release();

    return o0_pci_parse_capabilities(cfg, sizeof(cfg), res);
}

uint64_t o0_pci_msi_address(uint32_t dest_apic_id) {
    /* xAPIC format: 0xFEE in bits 31:20, destination APIC ID in bits 19:12. */
    return 0xFEE00000u | ((uint64_t)(dest_apic_id & 0xFFu) << 12);
}

uint32_t o0_pci_msi_data(uint32_t vector, bool level_assert) {
    uint32_t data = vector & 0xFFu; /* fixed delivery, physical destination */
    if (level_assert) {
        data |= 1u << 14;
    }
    return data;
}

int o0_pci_msix_encode(uint32_t dest_apic_id, uint32_t vector, bool masked,
                       struct o0_pci_msix_entry *out) {
    if (out == NULL || vector > 0xFFu) {
        return -1;
    }
    out->address = o0_pci_msi_address(dest_apic_id);
    out->data = o0_pci_msi_data(vector, false);
    out->vector_control = masked ? 1u : 0u;
    return 0;
}

int o0_pci_msi_enable(struct o0_pci_bdf bdf, uint8_t cap_offset,
                      uint32_t dest_apic_id, uint32_t vector) {
    if (cap_offset < 0x40u || vector > 0xFFu) {
        return -1;
    }
    uint16_t ctrl = 0;
    if (o0_pci_config_read16(bdf, (uint8_t)(cap_offset + 2u), &ctrl) != 0) {
        return -1;
    }
    bool is64 = (ctrl & 0x0080u) != 0u;
    bool per_vector_mask = (ctrl & 0x0100u) != 0u;
    uint8_t mask_offset = (uint8_t)(cap_offset + (is64 ? 0x10u : 0x0Cu));

    /* Quiesce the capability while its message is rewritten. */
    if (o0_pci_config_write16(bdf, (uint8_t)(cap_offset + 2u),
                              (uint16_t)(ctrl & ~0x0001u)) != 0) {
        return -1;
    }
    if (per_vector_mask &&
        o0_pci_config_write32(bdf, mask_offset, 0xFFFFFFFFu) != 0) {
        return -1;
    }
    if (o0_pci_config_write32(bdf, (uint8_t)(cap_offset + 4u),
                              (uint32_t)o0_pci_msi_address(dest_apic_id)) != 0) {
        return -1;
    }
    if (is64) {
        /* Destination lives in the low address word for xAPIC MSI. */
        if (o0_pci_config_write32(bdf, (uint8_t)(cap_offset + 8u), 0u) != 0) {
            return -1;
        }
    }
    uint8_t data_offset = (uint8_t)(cap_offset + (is64 ? 0x0Cu : 0x08u));
    if (o0_pci_config_write32(bdf, data_offset,
                              o0_pci_msi_data(vector, false)) != 0) {
        return -1;
    }
    if (per_vector_mask &&
        o0_pci_config_write32(bdf, mask_offset, 0u) != 0) {
        return -1;
    }
    /* One message: multiple-message-enable stays zero. */
    if (o0_pci_config_write16(bdf, (uint8_t)(cap_offset + 2u),
                              (uint16_t)((ctrl & ~0x0070u) | 0x0001u)) != 0) {
        return -1;
    }
    uint16_t after = 0;
    if (o0_pci_config_read16(bdf, (uint8_t)(cap_offset + 2u), &after) != 0) {
        return -1;
    }
    return (after & 0x0001u) != 0u ? 0 : -1;
}

int o0_pci_msi_disable(struct o0_pci_bdf bdf, uint8_t cap_offset) {
    if (cap_offset < 0x40u) {
        return -1;
    }
    uint16_t ctrl = 0;
    if (o0_pci_config_read16(bdf, (uint8_t)(cap_offset + 2u), &ctrl) != 0) {
        return -1;
    }
    return o0_pci_config_write16(bdf, (uint8_t)(cap_offset + 2u),
                                 (uint16_t)(ctrl & ~0x0001u));
}

int o0_pci_msix_program(volatile void *table, uint16_t table_entries, uint16_t index,
                        uint32_t dest_apic_id, uint32_t vector, bool masked) {
    if (table == NULL || table_entries == 0u || index >= table_entries ||
        vector > 0xFFu) {
        return -1;
    }
    struct o0_pci_msix_entry entry;
    if (o0_pci_msix_encode(dest_apic_id, vector, masked, &entry) != 0) {
        return -1;
    }
    volatile uint32_t *words = (volatile uint32_t *)table;
    size_t base = (size_t)index * 4u;
    words[base + 0u] = (uint32_t)(entry.address & 0xFFFFFFFFu);
    words[base + 1u] = (uint32_t)(entry.address >> 32);
    words[base + 2u] = entry.data;
    words[base + 3u] = entry.vector_control;
    return 0;
}

int o0_pci_msix_enable(struct o0_pci_bdf bdf, uint8_t cap_offset,
                       volatile void *table, uint16_t index,
                       uint32_t dest_apic_id, uint32_t vector) {
    if (cap_offset < 0x40u || table == NULL) {
        return -1;
    }
    uint16_t ctrl = 0;
    if (o0_pci_config_read16(bdf, (uint8_t)(cap_offset + 2u), &ctrl) != 0) {
        return -1;
    }
    uint16_t entries = (uint16_t)((ctrl & 0x07FFu) + 1u);
    if (o0_pci_msix_program(table, entries, index, dest_apic_id, vector, false) != 0) {
        return -1;
    }
    /* Enable, then clear the function mask. */
    if (o0_pci_config_write16(bdf, (uint8_t)(cap_offset + 2u),
                              (uint16_t)(ctrl | 0x8000u)) != 0) {
        return -1;
    }
    if (o0_pci_config_write16(bdf, (uint8_t)(cap_offset + 2u),
                              (uint16_t)((ctrl | 0x8000u) & ~0x4000u)) != 0) {
        return -1;
    }
    uint16_t after = 0;
    if (o0_pci_config_read16(bdf, (uint8_t)(cap_offset + 2u), &after) != 0) {
        return -1;
    }
    return ((after & 0x8000u) != 0u && (after & 0x4000u) == 0u) ? 0 : -1;
}

int o0_pci_msix_disable(struct o0_pci_bdf bdf, uint8_t cap_offset) {
    if (cap_offset < 0x40u) {
        return -1;
    }
    uint16_t ctrl = 0;
    if (o0_pci_config_read16(bdf, (uint8_t)(cap_offset + 2u), &ctrl) != 0) {
        return -1;
    }
    /* Mask the function first, then clear enable. */
    if (o0_pci_config_write16(bdf, (uint8_t)(cap_offset + 2u),
                              (uint16_t)(ctrl | 0x4000u)) != 0) {
        return -1;
    }
    return o0_pci_config_write16(bdf, (uint8_t)(cap_offset + 2u),
                                 (uint16_t)(ctrl & ~0x8000u));
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
                /* Bits 6:0 are the header layout; bit 7 is multi-function and is
                 * not part of the layout number. */
                d->resources.header_type = (uint8_t)(((hdr >> 16) & 0x7Fu));
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
bool o0_pci_truncated(void) { return pci_truncated; }
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