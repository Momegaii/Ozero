/* Pure parser and BAR validator for modern virtio PCI capabilities. */
#ifndef O0_VIRTIO_PCI_CAP_H
#define O0_VIRTIO_PCI_CAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ozero/pci.h"

#define O0_VIRTIO_PCI_CAP_VENDOR 0x09u
#define O0_VIRTIO_PCI_CAP_COMMON 1u
#define O0_VIRTIO_PCI_CAP_NOTIFY 2u
#define O0_VIRTIO_PCI_CAP_ISR 3u
#define O0_VIRTIO_PCI_CAP_DEVICE 4u
#define O0_VIRTIO_PCI_CFG_MAX_BARS 6u

struct o0_virtio_pci_region {
    bool present;
    uint8_t bar;
    uint32_t offset;
    uint32_t length;
};

struct o0_virtio_pci_caps {
    struct o0_virtio_pci_region common;
    struct o0_virtio_pci_region notify;
    struct o0_virtio_pci_region isr;
    struct o0_virtio_pci_region device;
    uint32_t notify_multiplier;
};

/* Parse a 256-byte conventional PCI config-space snapshot. BAR lengths come
 * from PCI BAR probing. This parser does not access hardware or map BARs. */
bool o0_virtio_pci_parse_caps(const uint8_t *cfg, size_t cfg_len,
                              const uint32_t bar_lengths[O0_VIRTIO_PCI_CFG_MAX_BARS],
                              struct o0_virtio_pci_caps *out);

/* Validate every referenced BAR against probed PCI metadata. Reject I/O,
 * unassigned, invalid, zero-sized and out-of-range BAR regions. */
bool o0_virtio_pci_validate_bars(const struct o0_virtio_pci_caps *caps,
                                 const struct o0_pci_bar bars[O0_VIRTIO_PCI_CFG_MAX_BARS]);

#endif
