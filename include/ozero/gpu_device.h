/* Ozero kernel - GPU discovery, device descriptors and capability records.
 *
 * This is the GPU-independent front of Stage 5. It turns the kernel PCI layer into
 * Ozero GPU device descriptors: it classifies display functions, validates their MMIO
 * windows, asks registered backends whether they claim a device, and records a
 * capability set. It contains no vendor page-table, engine or command details; those
 * live behind each backend (see include/ozero/gpu_hsw.h).
 *
 * Honesty boundary: a device is reported "supported" only when a backend explicitly
 * matches a researched and verified device ID/revision. The Haswell backend currently
 * records no verified IDs (the PRM does not list them; see
 * docs/research/haswell-gen75-ppgtt-ggtt.md section 1.2), so every device is presently
 * reported unsupported. Discovery never fails the boot path: unknown devices are
 * recorded, not treated as errors.
 */
#ifndef O0_GPU_DEVICE_H
#define O0_GPU_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/pci.h"
#include "ozero/status.h"

/* Upper bound on GPU display functions recorded by one discovery pass. */
#define O0_GPU_MAX_DEVICES 16u

/* PCI base class 0x03 is "Display controller". */
#define O0_PCI_CLASS_DISPLAY 0x03u

/* GPU generations Ozero can name. GEN_UNKNOWN means no verified backend claims it. */
enum o0_gpu_generation {
    O0_GPU_GEN_UNKNOWN = 0,
    O0_GPU_GEN_HASWELL = 1, /* Intel Gen7.5; concrete bring-up target, not yet matched */
};

/* Backends that may claim a device. NONE means safely unsupported. */
enum o0_gpu_backend_id {
    O0_GPU_BACKEND_NONE = 0,
    O0_GPU_BACKEND_HSW = 1,
};

/* A validated MMIO window for a GPU. Populated only from a decoded, probed BAR. */
struct o0_gpu_mmio {
    bool valid;
    bool prefetchable;
    bool is_64bit;
    uint64_t base;
    uint64_t size;
};

/* GPU-independent capability record. Fields are populated by the matching backend; if
 * no backend matches they stay at their zero/unknown defaults and the device is
 * reported unsupported. page_size_mask uses bit n for a 2^n byte page. */
struct o0_gpu_caps {
    enum o0_gpu_generation generation;
    uint8_t va_width_bits;   /* GPU virtual-address width; 0 = unknown */
    uint64_t max_va_size;    /* 0 = unknown */
    uint32_t min_page_size;  /* bytes; 0 = unknown */
    uint32_t page_size_mask; /* bit n set => 2^n byte page supported */
    bool has_render;
    bool has_blitter;
    bool has_video;
    bool has_media;
    bool submission_supported; /* command submission + completion implemented */
    bool submission_hardware_verified; /* submission executed on real hardware */
    bool preemption_reported;  /* documented and implemented for this backend */
    bool reset_reported;       /* documented and implemented for this backend */
};

/* One GPU device descriptor. `backend`/`supported` come from the match step; `mmio`
 * is filled by the discovery walk when the device exposes a usable memory BAR. */
struct o0_gpu_device {
    struct o0_pci_bdf bdf;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    struct o0_gpu_mmio mmio;
    struct o0_gpu_caps caps;
    enum o0_gpu_backend_id backend;
    bool supported;
};

/* Backend registration contract. A backend provides a match function (returning its id
 * when it claims a device, NONE otherwise) and a capability fill for a claimed device.
 * The Haswell backend is the only registration today. */
struct o0_gpu_backend_ops {
    const char *name;
    enum o0_gpu_backend_id id;
    enum o0_gpu_backend_id (*match)(const struct o0_pci_device *dev);
    void (*fill_caps)(struct o0_gpu_caps *caps);
};

/* Pure classification: is this PCI function a display controller? */
bool o0_gpu_is_display_class(const struct o0_pci_device *dev);

/* Validate one decoded/probed BAR as a usable MMIO window. Rejects non-memory,
 * unassigned (zero size), non-power-of-two, unaligned and overflowing windows. */
o0_status o0_gpu_validate_mmio_bar(const struct o0_pci_bar *bar, struct o0_gpu_mmio *out);

/* Select the primary MMIO window: the lowest-index valid, assigned memory BAR. Returns
 * O0_ENOENT when the device exposes no usable MMIO. */
o0_status o0_gpu_select_mmio(const struct o0_pci_bar *bars, uint32_t count,
                             struct o0_gpu_mmio *out);

/* Backend match across registered backends. Returns NONE when no backend claims it. */
enum o0_gpu_backend_id o0_gpu_match_backend(const struct o0_pci_device *dev);

/* Build a descriptor (identity + backend match + capability record) from a PCI device.
 * Does not touch MMIO; the discovery walk fills that in. */
o0_status o0_gpu_describe(const struct o0_pci_device *dev, struct o0_gpu_device *out);

/* Discovery: classify PCI display functions, probe their BARs, match backends and keep
 * the descriptors. Never fails boot: unsupported devices are recorded, not errors.
 * Returns the number of display functions recorded. */
size_t o0_gpu_discover(void);
size_t o0_gpu_device_count(void);
const struct o0_gpu_device *o0_gpu_device(size_t index);

#endif /* O0_GPU_DEVICE_H */