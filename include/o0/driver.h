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

/* Maps a memory BAR through the kernel direct map or a dedicated mapping. The returned
 * pointer is valid until the matching unmap. */
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
o0_status o0_io_cancel(struct o0_io_request *req);

#ifdef __cplusplus
}
#endif

#endif /* O0_DRIVER_H */
