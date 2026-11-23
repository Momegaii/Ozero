/*
 * Ozero (o0) platform contract: interfaces A1 must provide to A3 device drivers.
 *
 * This is a Stage 0 draft, not a stable ABI.  The contract deliberately keeps
 * physical addresses, IOMMU domains and firmware tables behind A1.  A3 drivers
 * consume DMA addresses and opaque handles; they never program an IOMMU or
 * walk ACPI/UEFI tables directly.
 *
 * A1's signed Stage 1 surface is o0_dma_constraint + o0_dma_alloc +
 * o0_dma_sync + o0_dma_free in include/o0/mm.h, with no IOMMU domain handle
 * and no SG map object.  This draft still carries o0_dma_domain, dma_map_sg
 * and iommu_map; it must be reconciled before the AHCI probe.
 */
#ifndef O0_PLATFORM_H
#define O0_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define O0_PLATFORM_ABI_VERSION 0x00010000u

#define O0_CAP_MMIO_WC       (1ull << 0)
#define O0_CAP_MSI           (1ull << 1)
#define O0_CAP_MSIX          (1ull << 2)
#define O0_CAP_IOMMU         (1ull << 3)
#define O0_CAP_ACPI          (1ull << 4)
#define O0_CAP_UEFI          (1ull << 5)
#define O0_CAP_HOTPLUG       (1ull << 6)
#define O0_CAP_DMA_COHERENT  (1ull << 7)
#define O0_CAP_DMA_64BIT     (1ull << 8)
#define O0_CAP_POLLING       (1ull << 9)
#define O0_CAP_CPU_HOTPLUG   (1ull << 10)
#define O0_CAP_PCI_PASID     (1ull << 11)

typedef uint64_t o0_dma_addr_t;
typedef uint64_t o0_phys_addr_t;
typedef uint64_t o0_iova_t;

struct o0_device;
struct o0_mmio;
struct o0_dma_domain;
struct o0_irq;
struct o0_fence;

typedef enum o0_status {
    O0_OK = 0,
    O0_ERR_INVAL = -1,
    O0_ERR_NOMEM = -2,
    O0_ERR_BUSY = -3,
    O0_ERR_TIMEOUT = -4,
    O0_ERR_IO = -5,
    O0_ERR_NODEV = -6,
    O0_ERR_PERM = -7,
    O0_ERR_AGAIN = -8,
    O0_ERR_NOTSUP = -9,
    O0_ERR_FAULT = -10,
    O0_ERR_RESET = -11,
    O0_ERR_OVERFLOW = -12
} o0_status;

typedef enum o0_cache_mode {
    O0_CACHE_UC = 0,
    O0_CACHE_WC,
    O0_CACHE_WB,
    O0_CACHE_WT,
    O0_CACHE_DEVICE
} o0_cache_mode;

typedef enum o0_dma_dir {
    O0_DMA_TO_DEVICE = 0,
    O0_DMA_FROM_DEVICE,
    O0_DMA_BIDIRECTIONAL
} o0_dma_dir;

typedef enum o0_reset_kind {
    O0_RESET_FUNCTION_LEVEL = 0,
    O0_RESET_HOT,
    O0_RESET_LINK,
    O0_RESET_DEVICE,
    O0_RESET_BUS
} o0_reset_kind;

typedef enum o0_power_state {
    O0_POWER_D0 = 0,
    O0_POWER_D1,
    O0_POWER_D2,
    O0_POWER_D3HOT,
    O0_POWER_D3COLD,
    O0_POWER_UNKNOWN
} o0_power_state;

typedef enum o0_irq_type {
    O0_IRQ_LEGACY = 0,
    O0_IRQ_MSI,
    O0_IRQ_MSIX
} o0_irq_type;

typedef enum o0_irq_ret {
    O0_IRQ_NONE = 0,
    O0_IRQ_HANDLED,
    O0_IRQ_WAKE
} o0_irq_ret;

#define O0_IOMMU_READ     (1u << 0)
#define O0_IOMMU_WRITE    (1u << 1)
#define O0_IOMMU_EXEC     (1u << 2)
#define O0_IOMMU_DEVICE   (1u << 3)
#define O0_IOMMU_COHERENT (1u << 4)

struct o0_pci_id {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
};

struct o0_pci_bar {
    uint8_t index;
    bool is_io;
    bool is_64bit;
    bool prefetchable;
    uint64_t base;
    uint64_t size;
};

struct o0_acpi_table {
    char signature[5];
    uint32_t length;
    uint8_t revision;
    o0_phys_addr_t phys_addr;
    const void *virt_addr;
};

typedef enum o0_fw_mem_type {
    O0_FW_MEM_CONVENTIONAL = 0,
    O0_FW_MEM_LOADER,
    O0_FW_MEM_BOOT,
    O0_FW_MEM_RUNTIME,
    O0_FW_MEM_ACPI_RECLAIM,
    O0_FW_MEM_ACPI_NVS,
    O0_FW_MEM_MMIO,
    O0_FW_MEM_PERSISTENT,
    O0_FW_MEM_RESERVED,
    O0_FW_MEM_UNUSABLE,
    O0_FW_MEM_OTHER
} o0_fw_mem_type;

struct o0_fw_mem_desc {
    uint64_t base;
    uint64_t length;
    o0_fw_mem_type type;
    uint64_t attributes;
};

struct o0_sg_segment {
    o0_dma_addr_t dma_addr;
    size_t length;
};

struct o0_sg_table {
    struct o0_sg_segment *segs;
    unsigned int nents;
    unsigned int orig_nents;
};

struct o0_irq_info {
    unsigned int irq;
    unsigned int vector;
    o0_irq_type type;
    unsigned int affinity_cpu;
};

typedef o0_irq_ret (*o0_irq_handler_t)(struct o0_irq *irq,
                                        unsigned int vector,
                                        void *data);
typedef void (*o0_fence_cb_t)(struct o0_fence *fence,
                              o0_status status,
                              void *data);

struct o0_platform_ops {
    uint32_t abi_version;
    uint64_t capabilities;

    /* Discovery and resources. */
    o0_status (*pci_enumerate)(unsigned int index,
                               struct o0_pci_id *id,
                               struct o0_device **out);
    o0_status (*pci_get_bar)(struct o0_device *dev,
                             unsigned int bar,
                             struct o0_pci_bar *out);
    o0_status (*pci_read_config)(struct o0_device *dev,
                                 uint16_t offset,
                                 void *buf,
                                 size_t len);
    o0_status (*pci_write_config)(struct o0_device *dev,
                                  uint16_t offset,
                                  const void *buf,
                                  size_t len);
    o0_status (*pci_enable)(struct o0_device *dev, bool enable);
    o0_status (*pci_set_master)(struct o0_device *dev, bool enable);
    o0_status (*pci_set_dma_mask)(struct o0_device *dev, uint64_t mask);
    o0_status (*pci_get_dma_mask)(struct o0_device *dev, uint64_t *mask);
    o0_status (*pci_reset)(struct o0_device *dev, o0_reset_kind kind);

    o0_status (*acpi_find_table)(const char signature[4],
                                 struct o0_acpi_table *out);
    o0_status (*fw_memmap)(struct o0_fw_mem_desc **out, size_t *count);
    o0_status (*fw_config_table)(const uint8_t guid[16],
                                 const void **out,
                                 size_t *len);

    /* MMIO.  Mappings are device memory; cache mode is explicit. */
    o0_status (*mmio_map)(struct o0_device *dev,
                          const struct o0_pci_bar *bar,
                          o0_cache_mode mode,
                          struct o0_mmio **out);
    void (*mmio_unmap)(struct o0_mmio *mmio);
    uint32_t (*mmio_read32)(const struct o0_mmio *mmio, size_t offset);
    uint64_t (*mmio_read64)(const struct o0_mmio *mmio, size_t offset);
    void (*mmio_write32)(struct o0_mmio *mmio, size_t offset, uint32_t value);
    void (*mmio_write64)(struct o0_mmio *mmio, size_t offset, uint64_t value);
    void (*mmio_flush)(struct o0_mmio *mmio);

    /* DMA and IOMMU.  A3 sees DMA addresses, never raw physical addresses. */
    o0_status (*dma_domain_get)(struct o0_device *dev,
                                struct o0_dma_domain **out);
    o0_status (*dma_alloc_coherent)(struct o0_dma_domain *domain,
                                    size_t size,
                                    size_t align,
                                    uint64_t boundary,
                                    o0_dma_addr_t *dma_addr,
                                    void **cpu_addr);
    void (*dma_free_coherent)(struct o0_dma_domain *domain,
                              size_t size,
                              o0_dma_addr_t dma_addr,
                              void *cpu_addr);
    o0_status (*dma_map_single)(struct o0_dma_domain *domain,
                                void *cpu_addr,
                                size_t len,
                                o0_dma_dir dir,
                                o0_dma_addr_t *dma_addr);
    void (*dma_unmap_single)(struct o0_dma_domain *domain,
                             o0_dma_addr_t dma_addr,
                             size_t len,
                             o0_dma_dir dir);
    o0_status (*dma_map_sg)(struct o0_dma_domain *domain,
                            struct o0_sg_table *sg,
                            o0_dma_dir dir);
    void (*dma_unmap_sg)(struct o0_dma_domain *domain,
                         struct o0_sg_table *sg,
                         o0_dma_dir dir);
    void (*dma_sync_single_for_cpu)(struct o0_dma_domain *domain,
                                    o0_dma_addr_t dma_addr,
                                    size_t len,
                                    o0_dma_dir dir);
    void (*dma_sync_single_for_device)(struct o0_dma_domain *domain,
                                       o0_dma_addr_t dma_addr,
                                       size_t len,
                                       o0_dma_dir dir);
    o0_status (*iommu_map)(struct o0_dma_domain *domain,
                           o0_iova_t iova,
                           o0_phys_addr_t phys,
                           size_t len,
                           uint32_t prot);
    o0_status (*iommu_unmap)(struct o0_dma_domain *domain,
                             o0_iova_t iova,
                             size_t len);

    /* Interrupts.  MSI/MSI-X vector allocation is separate from binding. */
    o0_status (*irq_alloc)(struct o0_device *dev,
                           unsigned int min_vectors,
                           unsigned int max_vectors,
                           o0_irq_type type,
                           struct o0_irq **out);
    void (*irq_free)(struct o0_irq *irq);
    o0_status (*irq_request)(struct o0_irq *irq,
                             unsigned int vector,
                             o0_irq_handler_t handler,
                             void *data,
                             const char *name,
                             unsigned long flags);
    void (*irq_mask)(struct o0_irq *irq, unsigned int vector);
    void (*irq_unmask)(struct o0_irq *irq, unsigned int vector);
    void (*irq_ack)(struct o0_irq *irq, unsigned int vector);
    o0_status (*irq_set_affinity)(struct o0_irq *irq,
                                  unsigned int vector,
                                  unsigned int cpu);

    /* Completion and deferred work. */
    struct o0_fence *(*fence_create)(const char *name);
    void (*fence_signal)(struct o0_fence *fence, o0_status status);
    o0_status (*fence_wait)(struct o0_fence *fence, uint64_t timeout_ns);
    void (*fence_put)(struct o0_fence *fence);
    o0_status (*fence_add_callback)(struct o0_fence *fence,
                                    o0_fence_cb_t cb,
                                    void *data);
    o0_status (*work_schedule)(void (*fn)(void *), void *data);

    /* Cacheable/uncacheable memory mapping and maintenance. */
    o0_status (*mem_map_pages)(struct o0_dma_domain *domain,
                               o0_phys_addr_t phys,
                               size_t len,
                               o0_cache_mode mode,
                               void **out);
    void (*mem_unmap)(void *addr, size_t len);
    void (*cache_flush)(void *addr, size_t len);

    /* Power, reset and hotplug. */
    o0_status (*pm_get)(struct o0_device *dev);
    void (*pm_put)(struct o0_device *dev);
    o0_status (*pm_set_state)(struct o0_device *dev, o0_power_state state);
    o0_status (*reset_assert)(struct o0_device *dev, unsigned int line);
    o0_status (*reset_deassert)(struct o0_device *dev, unsigned int line);
    o0_status (*hotplug_register)(struct o0_device *dev,
                                  void (*cb)(struct o0_device *, void *),
                                  void *data);

    void (*log)(int level, const char *fmt, ...);
};

/* Provided by A1.  A3 must check abi_version and capabilities before use. */
const struct o0_platform_ops *o0_platform_ops_get(void);

#define O0_PLATFORM (o0_platform_ops_get())

#ifdef __cplusplus
}
#endif

#endif /* O0_PLATFORM_H */
