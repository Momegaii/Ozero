/* Ozero kernel - boot information population.
 *
 * Copies Limine responses into kernel-owned memory and normalises them into
 * the o0_boot_info contract before bootloader-reclaimable memory is released.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <limine.h>

#include "ozero/boot.h"
#include "ozero/status.h"
#include "ozero/types.h"

/* Maximum normalised memory regions we can track. */
#define O0_BOOT_MAX_MEM_REGIONS 64

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

static struct o0_mem_region boot_memmap[O0_BOOT_MAX_MEM_REGIONS];
static uint64_t boot_memmap_count;

static struct o0_boot_info boot_info;
static bool boot_info_populated;

/* Map a Limine memory map type to the normalised o0_mem_type. */
static uint32_t limine_type_to_o0(uint64_t limine_type) {
    switch (limine_type) {
        case LIMINE_MEMMAP_USABLE: return O0_MEM_FREE;
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return O0_MEM_RECLAIMABLE;
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return O0_MEM_KERNEL;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return O0_MEM_ACPI_RECLAIM;
        case LIMINE_MEMMAP_ACPI_NVS: return O0_MEM_ACPI_NVS;
        case LIMINE_MEMMAP_RESERVED: return O0_MEM_RESERVED;
        case LIMINE_MEMMAP_RESERVED_MAPPED: return O0_MEM_RESERVED_MAPPED;
        case LIMINE_MEMMAP_BAD_MEMORY: return O0_MEM_BAD;
        case LIMINE_MEMMAP_FRAMEBUFFER: return O0_MEM_FRAMEBUFFER;
        default: return O0_MEM_RESERVED;
    }
}

static o0_status boot_populate_memmap(const struct limine_memmap_response *resp) {
    if (resp == NULL || resp->entries == NULL || resp->entry_count == 0) {
        return O0_EINVAL;
    }
    if (resp->entry_count > O0_BOOT_MAX_MEM_REGIONS) {
        return O0_ENOMEM;
    }

    boot_memmap_count = 0;
    for (uint64_t i = 0; i < resp->entry_count; ++i) {
        const struct limine_memmap_entry *entry = resp->entries[i];
        if (entry == NULL) {
            continue;
        }
        struct o0_mem_region *mr = &boot_memmap[boot_memmap_count++];
        mr->base = entry->base;
        mr->length = entry->length;
        mr->type = limine_type_to_o0(entry->type);
        mr->flags = 0;
    }
    return O0_OK;
}

o0_status o0_boot_populate(const struct limine_memmap_response *memmap_resp) {
    if (boot_info_populated) {
        return O0_EBUSY;
    }

    o0_status st = boot_populate_memmap(memmap_resp);
    if (st != O0_OK) {
        return st;
    }

    for (size_t i = 0; i < sizeof(boot_info) / sizeof(uint64_t); ++i) {
        ((uint64_t *)&boot_info)[i] = 0;
    }
    boot_info.magic = O0_BOOT_INFO_MAGIC;
    boot_info.version = O0_BOOT_INFO_VERSION;
    boot_info.limine_base_requested = 6;
    boot_info.firmware_type = O0_FW_UNKNOWN;
    boot_info.cpu_count = 1;
    boot_info.hhdm_offset = 0;
    boot_info.kernel_phys = 0;       /* physical load offset not yet determined */
    boot_info.kernel_virt = (o0_vaddr_t)(uintptr_t)_kernel_start;
    boot_info.kernel_phys_size = (uint64_t)((uintptr_t)_kernel_end - (uintptr_t)_kernel_start);
    boot_info.memmap = boot_memmap;
    boot_info.memmap_count = boot_memmap_count;
    boot_info.cpus = NULL;
    boot_info.rsdp = 0;
    boot_info.efi_system_table = 0;
    boot_info.efi_memmap = NULL;
    boot_info.efi_memmap_size = 0;
    boot_info.efi_memmap_desc_size = 0;
    boot_info.efi_memmap_desc_version = 0;
    boot_info.tsc_frequency = 0;
    boot_info.initial_stack_top = 0;
    boot_info.boot_arena = 0;
    boot_info.boot_arena_size = 0;
    boot_info.flags = 0;

    boot_info_populated = true;
    return O0_OK;
}

const struct o0_boot_info *o0_boot_info(void) {
    return boot_info_populated ? &boot_info : NULL;
}

bool o0_boot_contract_ok(const struct o0_boot_info *bi) {
    if (bi == NULL) {
        return false;
    }
    return bi->magic == O0_BOOT_INFO_MAGIC &&
           bi->version == O0_BOOT_INFO_VERSION &&
           bi->memmap != NULL &&
           bi->memmap_count > 0;
}
