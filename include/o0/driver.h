/* Ozero kernel - generic driver-facing services.
 *
 * Device-class protocols (storage, network, display, input) belong to A3. This header
 * contains only the kernel services every driver needs: discovery, MMIO, IRQ, DMA
 * constraints and asynchronous I/O requests.
 */
#ifndef O0_DRIVER_H
#define O0_DRIVER_H

#include "o0/irq.h"
#include "o0/mm.h"
#include "o0/status.h"
#include "o0/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bus-neutral identity. A3 classifies devices; the kernel does not. */
struct o0_device_id {
    uint16_t vendor;
    uint16_t device;
    uint16_t subsystem_vendor;
    uint16_t subsystem_device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint16_t bus;
    uint8_t slot;
    uint8_t function;
};

typedef o0_status (*o0_device_iter_fn)(const struct o0_device_id *id, o0_device_t *dev,
                                       void *ctx);

o0_status o0_device_enumerate(o0_device_iter_fn fn, void *ctx);

/* Maps a memory BAR as a dedicated uncached (O0_MAP_UC), non-executable mapping. It is
 * never served from the HHDM. The returned pointer is valid until the matching unmap;
 * the caller must quiesce DMA before unmapping. */
O0_NODISCARD o0_status o0_device_map_bar(o0_device_t *dev, uint8_t bar, size_t *out_len,
                                         void **out_virt);
o0_status o0_device_unmap_bar(o0_device_t *dev, void *virt, size_t len);

/* Resolves the device interrupt and, where supported, allocates an MSI/MSI-X vector. */
O0_NODISCARD o0_status o0_device_irq(o0_device_t *dev, o0_gsi_t *out_gsi,
                                     o0_vector_t *out_vector);

/* The driver supplies its own constraints; the kernel never invents them. */
O0_NODISCARD o0_status o0_device_dma_constraints(o0_device_t *dev,
                                                 struct o0_dma_constraint *out);

o0_status o0_device_bus_master_enable(o0_device_t *dev, bool enable);

/* Limits the device reports for the generic request object below. A3 validates the
 * device-class-specific parts (block size, queue model) before submitting I/O. */
struct o0_io_limits {
    uint32_t max_transfer;   /* bytes per request; 0 means unlimited */
    uint16_t max_queue_depth;
    uint16_t alignment;      /* power of two, or 0 */
    uint32_t flags;
};

O0_NODISCARD o0_status o0_device_io_limits(o0_device_t *dev, struct o0_io_limits *out);

/* Stops the device from issuing new DMA and waits for in-flight requests to finish.
 * Required before o0_dma_free, o0_device_unmap_bar, o0_device_reset and driver unload. */
O0_NODISCARD o0_status o0_device_dma_quiesce(o0_device_t *dev);

/* Resets the device. Precondition: the device is quiesced and all requests have
 * completed or been cancelled. */
O0_NODISCARD o0_status o0_device_reset(o0_device_t *dev);

/* ---- Generic asynchronous I/O request ---- */

enum o0_io_op {
    O0_IO_READ = 0,
    O0_IO_WRITE = 1,
    O0_IO_FLUSH = 2,
    O0_IO_CONTROL = 3
};

struct o0_io_request;

typedef void (*o0_io_complete_fn)(struct o0_io_request *req, void *ctx);

struct o0_io_request {
    enum o0_io_op op;
    uint64_t offset;
    void *buffer;
    size_t length;
    uint32_t flags;
    o0_status status;
    o0_io_complete_fn complete;
    void *ctx;
    void *driver_private[2];
};

O0_NODISCARD o0_status o0_io_submit(o0_device_t *dev, struct o0_io_request *req);

/* Best-effort cancellation. If it returns O0_EBUSY the request is already completing and
 * the completion callback will still run; the buffer must remain valid until then. */
o0_status o0_io_cancel(struct o0_io_request *req);

#ifdef __cplusplus
}
#endif

#endif /* O0_DRIVER_H */
