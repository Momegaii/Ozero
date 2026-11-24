/* Ozero kernel - GPU discovery, device descriptors and capability records.
 *
 * GPU-independent: no page-table, engine or command detail lives here. Backends
 * register through `struct o0_gpu_backend_ops`; the Haswell backend is the only
 * registration and is the sole place that may claim a device. See gpu_device.h for the
 * honesty boundary around the `supported` flag.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/gpu_device.h"
#include "ozero/gpu_hsw.h" /* for the registered backend ops only */
#include "ozero/pci.h"
#include "ozero/status.h"
#include "string.h"

/* Registered backends. Add a backend by appending its ops here; the generic walk below
 * stays unchanged. The core never contains vendor specifics. */
static const struct o0_gpu_backend_ops *const gpu_backends[] = {
    &o0_gpu_hsw_backend,
};

#define O0_GPU_NUM_BACKENDS (sizeof(gpu_backends) / sizeof(gpu_backends[0]))

static bool is_power_of_two_u64(uint64_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

bool o0_gpu_is_display_class(const struct o0_pci_device *dev) {
    return dev != NULL && dev->class_code == O0_PCI_CLASS_DISPLAY;
}

o0_status o0_gpu_validate_mmio_bar(const struct o0_pci_bar *bar, struct o0_gpu_mmio *out) {
    if (bar == NULL || out == NULL) {
        return O0_EINVAL;
    }
    /* Must be a memory BAR that the size probe resolved and firmware assigned. */
    if (!bar->valid || !bar->is_memory || !bar->assigned || bar->size == 0u) {
        return O0_ENOENT;
    }
    /* A BAR size is always a power of two and its base is aligned to that size. */
    if (!is_power_of_two_u64(bar->size)) {
        return O0_EINVAL;
    }
    if ((bar->base % bar->size) != 0u) {
        return O0_EINVAL;
    }
    if (bar->base + bar->size < bar->base) {
        return O0_EOVERFLOW;
    }

    out->valid = true;
    out->prefetchable = bar->prefetchable;
    out->is_64bit = bar->is_64bit;
    out->base = bar->base;
    out->size = bar->size;
    return O0_OK;
}

o0_status o0_gpu_select_mmio(const struct o0_pci_bar *bars, uint32_t count,
                             struct o0_gpu_mmio *out) {
    if (out == NULL || (bars == NULL && count != 0u)) {
        return O0_EINVAL;
    }
    for (uint32_t i = 0; i < count; ++i) {
        struct o0_gpu_mmio candidate;
        o0_status status = o0_gpu_validate_mmio_bar(&bars[i], &candidate);
        if (status == O0_OK) {
            *out = candidate;
            return O0_OK;
        }
    }
    return O0_ENOENT;
}

static const struct o0_gpu_backend_ops *match_backend(const struct o0_pci_device *dev,
                                                      enum o0_gpu_backend_id *id) {
    for (size_t i = 0; i < O0_GPU_NUM_BACKENDS; ++i) {
        const struct o0_gpu_backend_ops *ops = gpu_backends[i];
        if (ops == NULL || ops->match == NULL) {
            continue;
        }
        enum o0_gpu_backend_id found = ops->match(dev);
        if (found != O0_GPU_BACKEND_NONE) {
            *id = found;
            return ops;
        }
    }
    *id = O0_GPU_BACKEND_NONE;
    return NULL;
}

enum o0_gpu_backend_id o0_gpu_match_backend(const struct o0_pci_device *dev) {
    enum o0_gpu_backend_id id = O0_GPU_BACKEND_NONE;
    (void)match_backend(dev, &id);
    return id;
}

o0_status o0_gpu_describe(const struct o0_pci_device *dev, struct o0_gpu_device *out) {
    if (dev == NULL || out == NULL) {
        return O0_EINVAL;
    }
    memset(out, 0, sizeof(*out));
    out->bdf = dev->bdf;
    out->vendor_id = dev->vendor_id;
    out->device_id = dev->device_id;
    out->revision = dev->revision;
    out->class_code = dev->class_code;
    out->subclass = dev->subclass;
    out->prog_if = dev->prog_if;

    enum o0_gpu_backend_id id = O0_GPU_BACKEND_NONE;
    const struct o0_gpu_backend_ops *ops = match_backend(dev, &id);
    out->backend = id;
    if (ops != NULL && ops->fill_caps != NULL) {
        ops->fill_caps(&out->caps);
        out->supported = id != O0_GPU_BACKEND_NONE;
    } else {
        out->supported = false;
    }
    return O0_OK;
}

static struct o0_gpu_device gpu_devices[O0_GPU_MAX_DEVICES];
static size_t gpu_device_count;

size_t o0_gpu_discover(void) {
    gpu_device_count = 0;
    const size_t pci_count = o0_pci_device_count();
    for (size_t i = 0; i < pci_count; ++i) {
        const struct o0_pci_device *dev = o0_pci_device(i);
        if (!o0_gpu_is_display_class(dev)) {
            continue;
        }
        if (gpu_device_count >= O0_GPU_MAX_DEVICES) {
            break;
        }
        struct o0_gpu_device *g = &gpu_devices[gpu_device_count];
        if (o0_gpu_describe(dev, g) != O0_OK) {
            continue;
        }

        /* Best-effort MMIO discovery: probe each BAR and select the first usable
         * memory window. A device with no usable MMIO is still recorded, just with an
         * invalid mmio window, so boot continues and the device stays unsupported. */
        struct o0_pci_bar bars[6];
        uint32_t count = dev->resources.bar_count;
        if (count > 6u) {
            count = 6u;
        }
        for (uint32_t b = 0; b < count; ++b) {
            if (o0_pci_probe_bar(dev->bdf, (uint8_t)b, dev->resources.header_type,
                                 &bars[b]) != 0) {
                memset(&bars[b], 0, sizeof(bars[b]));
            }
        }
        (void)o0_gpu_select_mmio(bars, count, &g->mmio);
        gpu_device_count++;
    }
    return gpu_device_count;
}

size_t o0_gpu_device_count(void) {
    return gpu_device_count;
}

const struct o0_gpu_device *o0_gpu_device(size_t index) {
    if (index >= gpu_device_count) {
        return NULL;
    }
    return &gpu_devices[index];
}