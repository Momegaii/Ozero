/* Stage 0 header coherence checks.
 *
 * Compile-time only: this file is not linked into the kernel. It proves that the public
 * headers are self-consistent, freestanding, and that the documented ABI facts hold on
 * x86-64.
 */
#include "ozero/o0.h"

_Static_assert(O0_PAGE_SIZE == 4096u, "page size");
_Static_assert(sizeof(o0_paddr_t) == 8u, "physical address width");
_Static_assert(sizeof(o0_vaddr_t) == 8u, "virtual address width");
_Static_assert(sizeof(o0_handle_t) == 8u, "handle width");
_Static_assert(sizeof(unsigned long) == 8u, "irq state width on x86-64");

/* Memory types must keep the documented numeric values. */
_Static_assert(O0_MEM_FREE == 0, "mem type free");
_Static_assert(O0_MEM_RECLAIMABLE == 1, "mem type reclaimable");
_Static_assert(O0_MEM_KERNEL == 2, "mem type kernel");
_Static_assert(O0_MEM_ACPI_RECLAIM == 3, "mem type acpi reclaim");
_Static_assert(O0_MEM_ACPI_NVS == 4, "mem type acpi nvs");
_Static_assert(O0_MEM_RESERVED == 5, "mem type reserved");
_Static_assert(O0_MEM_RESERVED_MAPPED == 6, "mem type reserved mapped");
_Static_assert(O0_MEM_BAD == 7, "mem type bad");
_Static_assert(O0_MEM_FRAMEBUFFER == 8, "mem type framebuffer");
_Static_assert(O0_MEM_MMIO == 9, "mem type mmio");

_Static_assert(O0_MEM_F_HHDM_MAPPED == (1u << 0), "mem flag hhdm");
_Static_assert(O0_MEM_F_RECLAIM_PENDING == (1u << 1), "mem flag reclaim");

_Static_assert(sizeof(struct o0_mem_region) == 24u, "mem region size");
_Static_assert(offsetof(struct o0_mem_region, base) == 0u, "mem region base offset");
_Static_assert(offsetof(struct o0_mem_region, length) == 8u, "mem region length offset");
_Static_assert(offsetof(struct o0_mem_region, type) == 16u, "mem region type offset");
_Static_assert(offsetof(struct o0_mem_region, flags) == 20u, "mem region flags offset");

_Static_assert(sizeof(struct o0_boot_cpu) == 16u, "boot cpu size");
_Static_assert(O0_BOOT_CPU_BSP == (1u << 0), "boot cpu flag");

_Static_assert(O0_HANDLE_INVALID == 0u, "invalid handle");
_Static_assert(O0_GSI_NONE == UINT32_MAX, "no gsi");
_Static_assert(O0_VECTOR_NONE == UINT32_MAX, "no vector");

_Static_assert((O0_MAP_READ | O0_MAP_WRITE | O0_MAP_EXEC | O0_MAP_USER) == 0x0fu,
               "map flag values");
_Static_assert(O0_ENOTSUP == 13, "status not supported value");
_Static_assert(O0_MAP_NX == 0x80u, "map flag nx");
_Static_assert(O0_DMA_TO_DEVICE == 0 && O0_DMA_FROM_DEVICE == 1 &&
                   O0_DMA_BIDIRECTIONAL == 2,
               "dma direction values");
_Static_assert(sizeof(struct o0_io_limits) == 12u, "io limits size");
_Static_assert(sizeof(struct o0_device_ops) == 5u * sizeof(void *), "device ops size");

/* Taking the address of each API forces the compiler to check the prototype. */
static o0_status (*const check_pmm_init)(const struct o0_boot_info *) = o0_pmm_init;
static o0_paddr_t (*const check_pmm_alloc)(uint32_t) = o0_pmm_alloc_pages;
static o0_status (*const check_as_map)(o0_as_t *, o0_vaddr_t, o0_paddr_t, size_t, uint32_t) =
    o0_as_map;
static o0_status (*const check_irq_register)(o0_gsi_t, uint32_t, o0_irq_handler_t, void *) =
    o0_irq_register;
static o0_status (*const check_thread_create)(o0_process_t *, const struct o0_thread_attr *,
                                              o0_thread_t **) = o0_thread_create;
static o0_status (*const check_thread_user_pointer)(o0_thread_t *, void *) =
    o0_thread_set_user_pointer;
static o0_status (*const check_thread_gs_base)(o0_thread_t *, uint64_t) = o0_thread_set_gs_base;
static o0_status (*const check_thread_fs_base)(o0_thread_t *, uint64_t) = o0_thread_set_fs_base;
static o0_status (*const check_handle_open)(o0_process_t *, o0_object_t *, uint32_t,
                                            o0_handle_t *) = o0_handle_open;
static o0_status (*const check_dma_alloc)(const struct o0_dma_constraint *, size_t,
                                          struct o0_dma_buffer *) = o0_dma_alloc;
static o0_status (*const check_io_submit)(o0_device_t *, struct o0_io_request *) = o0_io_submit;
static o0_status (*const check_hhdm_to_virt)(o0_paddr_t, size_t, void **) = o0_hhdm_to_virt;
static o0_status (*const check_dma_sync)(struct o0_dma_buffer *, size_t, size_t,
                                         enum o0_dma_dir) = o0_dma_sync;
static o0_status (*const check_dma_quiesce)(o0_device_t *) = o0_device_dma_quiesce;
static o0_status (*const check_device_reset)(o0_device_t *) = o0_device_reset;
static o0_status (*const check_io_limits)(o0_device_t *, struct o0_io_limits *) =
    o0_device_io_limits;

void o0_stage0_header_check_anchor(void);

void o0_stage0_header_check_anchor(void)
{
    (void)check_pmm_init;
    (void)check_pmm_alloc;
    (void)check_as_map;
    (void)check_irq_register;
    (void)check_thread_create;
    (void)check_thread_user_pointer;
    (void)check_thread_gs_base;
    (void)check_thread_fs_base;
    (void)check_handle_open;
    (void)check_dma_alloc;
    (void)check_io_submit;
    (void)check_hhdm_to_virt;
    (void)check_dma_sync;
    (void)check_dma_quiesce;
    (void)check_device_reset;
    (void)check_io_limits;
}
