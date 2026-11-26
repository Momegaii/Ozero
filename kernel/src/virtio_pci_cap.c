#include "ozero/virtio_pci_cap.h"

#include <string.h>

#define PCI_STATUS_CAP_LIST 0x10u
#define PCI_CAP_PTR 0x34u
#define PCI_CAP_MIN 0x40u
#define PCI_CAP_MAX_WALK 48u
#define VIRTIO_CAP_LEN 16u
#define VIRTIO_NOTIFY_CAP_LEN 20u

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool region_set(struct o0_virtio_pci_region *region, uint8_t bar,
                       uint32_t offset, uint32_t length,
                       const uint32_t bar_lengths[O0_VIRTIO_PCI_CFG_MAX_BARS]) {
    if (region->present || bar >= O0_VIRTIO_PCI_CFG_MAX_BARS || length == 0u ||
        offset > bar_lengths[bar] || length > bar_lengths[bar] - offset) {
        return false;
    }
    region->present = true;
    region->bar = bar;
    region->offset = offset;
    region->length = length;
    return true;
}

bool o0_virtio_pci_parse_caps(const uint8_t *cfg, size_t cfg_len,
                              const uint32_t bar_lengths[O0_VIRTIO_PCI_CFG_MAX_BARS],
                              struct o0_virtio_pci_caps *out) {
    if (cfg == NULL || bar_lengths == NULL || out == NULL || cfg_len < 256u) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if ((cfg[0x06] & PCI_STATUS_CAP_LIST) == 0u) {
        return false;
    }

    bool visited[256] = { false };
    uint8_t ptr = cfg[PCI_CAP_PTR];
    bool saw_notify = false;
    for (uint32_t n = 0; ptr != 0u && n < PCI_CAP_MAX_WALK; ++n) {
        if (ptr < PCI_CAP_MIN || ptr > 0xFCu || (ptr & 3u) != 0u || visited[ptr]) {
            return false;
        }
        visited[ptr] = true;
        if ((size_t)ptr + 2u > cfg_len) return false;
        uint8_t id = cfg[ptr];
        uint8_t next = cfg[ptr + 1u];
        if (id == O0_VIRTIO_PCI_CAP_VENDOR) {
            if ((size_t)ptr + 3u > cfg_len) return false;
            uint8_t cap_len = cfg[ptr + 2u];
            if (cap_len < VIRTIO_CAP_LEN || (size_t)ptr + cap_len > cfg_len) {
                return false;
            }
            uint8_t type = cfg[ptr + 3u];
            uint8_t bar = cfg[ptr + 4u];
            uint32_t offset = read_le32(&cfg[ptr + 8u]);
            uint32_t length = read_le32(&cfg[ptr + 12u]);
            struct o0_virtio_pci_region *target = NULL;
            switch (type) {
                case O0_VIRTIO_PCI_CAP_COMMON: target = &out->common; break;
                case O0_VIRTIO_PCI_CAP_NOTIFY: target = &out->notify; break;
                case O0_VIRTIO_PCI_CAP_ISR: target = &out->isr; break;
                case O0_VIRTIO_PCI_CAP_DEVICE: target = &out->device; break;
                default: break;
            }
            if (target != NULL &&
                !region_set(target, bar, offset, length, bar_lengths)) return false;
            if (type == O0_VIRTIO_PCI_CAP_NOTIFY) {
                if (saw_notify || cap_len < VIRTIO_NOTIFY_CAP_LEN) return false;
                saw_notify = true;
                out->notify_multiplier = read_le32(&cfg[ptr + 16u]);
            }
        }
        if (next != 0u && (next < PCI_CAP_MIN || next > 0xFCu ||
                           (next & 3u) != 0u)) return false;
        ptr = next;
    }
    if (ptr != 0u) return false;
    return out->common.present && out->notify.present &&
           out->isr.present && out->device.present && saw_notify;
}

static bool validate_region_bar(const struct o0_virtio_pci_region *region,
                                const struct o0_pci_bar bars[O0_VIRTIO_PCI_CFG_MAX_BARS]) {
    if (!region->present || region->bar >= O0_VIRTIO_PCI_CFG_MAX_BARS) return false;
    const struct o0_pci_bar *bar = &bars[region->bar];
    if (!bar->valid || !bar->is_memory || !bar->assigned || bar->size == 0u) return false;
    return (uint64_t)region->offset <= bar->size &&
           (uint64_t)region->length <= bar->size - (uint64_t)region->offset;
}

bool o0_virtio_pci_validate_bars(const struct o0_virtio_pci_caps *caps,
                                 const struct o0_pci_bar bars[O0_VIRTIO_PCI_CFG_MAX_BARS]) {
    if (caps == NULL || bars == NULL) return false;
    return validate_region_bar(&caps->common, bars) &&
           validate_region_bar(&caps->notify, bars) &&
           validate_region_bar(&caps->isr, bars) &&
           validate_region_bar(&caps->device, bars);
}
