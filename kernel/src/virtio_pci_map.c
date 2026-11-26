/* Modern virtio PCI BAR mapping.
 *
 * Kernel-only: this is the piece that needs the paging and PCI services, so it
 * is kept out of the host-testable register/protocol unit. Each capability BAR
 * is mapped once as a dedicated uncached window, never through the HHDM, and
 * the window is chosen by the caller so two devices can never alias.
 */

#include "ozero/virtio_pci_mmio.h"

#include <string.h>

#include "ozero/mm.h"
#include "ozero/types.h"

static bool bar_used(const struct o0_virtio_pci_caps *caps, uint8_t bar) {
    return (caps->common.present && caps->common.bar == bar) ||
           (caps->notify.present && caps->notify.bar == bar) ||
           (caps->isr.present && caps->isr.bar == bar) ||
           (caps->device.present && caps->device.bar == bar);
}

static bool caps_regions_valid(const struct o0_virtio_pci_caps *caps) {
    const struct o0_virtio_pci_region *r[] = {
        &caps->common, &caps->notify, &caps->isr, &caps->device
    };
    for (size_t i = 0; i < O0_ARRAY_SIZE(r); ++i) {
        if (!r[i]->present || r[i]->bar >= O0_VIRTIO_PCI_CFG_MAX_BARS ||
            r[i]->length == 0u) {
            return false;
        }
    }
    return true;
}

static uint64_t window_for(const struct o0_virtio_pci_mapping *mapping, uint8_t bar) {
    return mapping->window_base + (uint64_t)bar * mapping->window_stride;
}

o0_status o0_virtio_pci_unmap_caps(struct o0_virtio_pci_mapping *mapping) {
    if (mapping == NULL) return O0_EINVAL;
    o0_status result = O0_OK;
    for (size_t i = O0_VIRTIO_PCI_CFG_MAX_BARS; i > 0; --i) {
        size_t bar = i - 1u;
        if (mapping->bar_virt[bar] == NULL) continue;
        o0_status st = o0_as_unmap(o0_as_kernel(), window_for(mapping, (uint8_t)bar),
                                   mapping->bar_length[bar]);
        if (st != O0_OK) {
            result = st;
        } else {
            mapping->bar_virt[bar] = NULL;
            mapping->bar_length[bar] = 0;
        }
    }
    return result;
}

o0_status o0_virtio_pci_map_caps(const struct o0_pci_bdf *bdf, uint8_t header_type,
                                 const struct o0_virtio_pci_caps *caps,
                                 uint64_t window_base, uint64_t window_stride,
                                 struct o0_virtio_pci_mapping *mapping) {
    if (bdf == NULL || caps == NULL || mapping == NULL || window_stride == 0u ||
        (window_stride & (O0_PAGE_SIZE - 1u)) != 0u || !caps_regions_valid(caps)) {
        return O0_EINVAL;
    }
    memset(mapping, 0, sizeof(*mapping));
    mapping->bdf = *bdf;
    mapping->header_type = header_type;
    mapping->window_base = window_base;
    mapping->window_stride = window_stride;

    for (uint8_t bar = 0; bar < O0_VIRTIO_PCI_CFG_MAX_BARS; ++bar) {
        if (!bar_used(caps, bar)) continue;

        struct o0_pci_bar info;
        if (o0_pci_probe_bar(*bdf, bar, header_type, &info) != 0) {
            (void)o0_virtio_pci_unmap_caps(mapping);
            return O0_EIO;
        }
        if (!info.valid || !info.is_memory || !info.assigned || info.size == 0u) {
            (void)o0_virtio_pci_unmap_caps(mapping);
            return O0_ENOTSUP;
        }
        uint64_t aligned = info.base & ~(uint64_t)(O0_PAGE_SIZE - 1u);
        uint64_t bytes = (info.base - aligned) + info.size;
        if (bytes == 0u || bytes > window_stride) {
            (void)o0_virtio_pci_unmap_caps(mapping);
            return O0_ERANGE;
        }
        size_t map_bytes = (size_t)((bytes + O0_PAGE_SIZE - 1u) &
                                    ~(uint64_t)(O0_PAGE_SIZE - 1u));
        uint64_t window = window_for(mapping, bar);
        o0_status st = o0_as_map(o0_as_kernel(), window, aligned, map_bytes,
                                 O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC | O0_MAP_NX);
        if (st != O0_OK) {
            (void)o0_virtio_pci_unmap_caps(mapping);
            return st;
        }
        mapping->bar_virt[bar] = (void *)(uintptr_t)window;
        mapping->bar_length[bar] = map_bytes;
    }

    /* Every capability region must lie inside the window that actually backs it. */
    const struct o0_virtio_pci_region *r[] = {
        &caps->common, &caps->notify, &caps->isr, &caps->device
    };
    for (size_t i = 0; i < O0_ARRAY_SIZE(r); ++i) {
        size_t mapped = mapping->bar_length[r[i]->bar];
        if (mapping->bar_virt[r[i]->bar] == NULL || (uint64_t)r[i]->offset > mapped ||
            (uint64_t)r[i]->length > mapped - (uint64_t)r[i]->offset) {
            (void)o0_virtio_pci_unmap_caps(mapping);
            return O0_ERANGE;
        }
    }
    return O0_OK;
}
