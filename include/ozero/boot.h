/* Ozero kernel - boot information contract.
 *
 * Populated by o0_boot_populate() (kernel/src/boot_info.c) from the Limine responses and
 * copied into kernel-owned memory before bootloader-reclaimable memory is released. See
 * docs/boot-contract.md.
 */
#ifndef O0_BOOT_H
#define O0_BOOT_H

#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_BOOT_INFO_MAGIC 0x4f304249u /* "O0BI" */
#define O0_BOOT_INFO_VERSION 1u

enum o0_firmware_type {
    O0_FW_UNKNOWN = 0,
    O0_FW_BIOS = 1,
    O0_FW_EFI32 = 2,
    O0_FW_EFI64 = 3
};

/* Normalised memory types. The mapping from Limine types is in docs/boot-contract.md §2. */
enum o0_mem_type {
    O0_MEM_FREE = 0,          /* usable RAM, owned by the kernel */
    O0_MEM_RECLAIMABLE = 1,   /* bootloader-reclaimable, on loan until reclaim completes */
    O0_MEM_KERNEL = 2,        /* kernel image range */
    O0_MEM_ACPI_RECLAIM = 3,  /* reclaimable only after ACPI tables are copied */
    O0_MEM_ACPI_NVS = 4,
    O0_MEM_RESERVED = 5,
    O0_MEM_RESERVED_MAPPED = 6,
    O0_MEM_BAD = 7,
    O0_MEM_FRAMEBUFFER = 8,
    O0_MEM_MMIO = 9
};

/* Region flags. */
#define O0_MEM_F_HHDM_MAPPED (1u << 0)  /* reachable through the higher-half direct map */
#define O0_MEM_F_RECLAIM_PENDING (1u << 1)

struct o0_mem_region {
    o0_paddr_t base;
    uint64_t length;
    uint32_t type;  /* enum o0_mem_type */
    uint32_t flags;
};

struct o0_boot_cpu {
    uint32_t acpi_uid;
    uint32_t apic_id;
    uint32_t flags;
    uint32_t reserved;
};

#define O0_BOOT_CPU_BSP (1u << 0)

struct o0_boot_info {
    uint32_t magic;  /* O0_BOOT_INFO_MAGIC */
    uint16_t version;
    uint16_t size;
    uint32_t limine_base_requested;
    uint32_t limine_base_loaded;
    uint32_t firmware_type;  /* enum o0_firmware_type */
    uint32_t cpu_count;

    /* Partial direct map: virt = phys + hhdm_offset, valid only for regions whose type is
     * HHDM-mapped. Use o0_hhdm_to_virt for a checked conversion. */
    uint64_t hhdm_offset;

    o0_paddr_t kernel_phys;
    o0_vaddr_t kernel_virt;
    uint64_t kernel_phys_size;

    const struct o0_mem_region *memmap;  /* kernel-owned copy, sorted by base */
    uint64_t memmap_count;

    const struct o0_boot_cpu *cpus;  /* kernel-owned copy, BSP included */

    o0_vaddr_t rsdp;             /* HHDM virtual, 0 if ACPI is unavailable */
    o0_vaddr_t efi_system_table; /* HHDM virtual, 0 if EFI is unavailable */

    const void *efi_memmap; /* HHDM virtual copy, for a future SetVirtualAddressMap */
    uint64_t efi_memmap_size;
    uint64_t efi_memmap_desc_size;
    uint32_t efi_memmap_desc_version;
    uint32_t reserved0;

    uint64_t tsc_frequency; /* Hz, 0 if the bootloader could not determine it */

    o0_paddr_t initial_stack_top; /* bootloader stack; reclaimable */
    o0_vaddr_t boot_arena;        /* static kernel arena holding this structure and copies */
    uint64_t boot_arena_size;

    uint32_t flags;
    uint32_t reserved1;
};

/* Set during early init and never replaced afterwards. */
const struct o0_boot_info *o0_boot_info(void);

/* True when the bootloader honoured the requested base revision. */
bool o0_boot_contract_ok(const struct o0_boot_info *bi);

#ifdef __cplusplus
}
#endif

#endif /* O0_BOOT_H */
