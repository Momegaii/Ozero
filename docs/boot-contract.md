# Ozero Boot Contract

Documentation of the boot-time memory contract between the Limine bootloader
and the Ozero kernel. Defines the `o0_boot_info` structure, memory region
normalisation, and reclaim rules for bootloader-reclaimable memory.

## 1. o0_boot_info structure

Populated by `o0_boot_populate()` from Limine responses before
bootloader-reclaimable memory is released. Lives in `include/ozero/boot.h`.

Key fields:

| Field | Meaning |
|-------|---------|
| `magic` / `version` | Structure magic `0x4f304249` ("O0BI"), version 1 |
| `limine_base_requested` / `limine_base_loaded` | Requested vs loaded Limine base revision |
| `firmware_type` | BIOS / EFI32 / EFI64 / unknown |
| `hhdm_offset` | Higher-half direct map offset (virt = phys + offset) |
| `kernel_phys` / `kernel_virt` / `kernel_phys_size` | Kernel load location (phys TBD until CR3 switch) |
| `memmap` / `memmap_count` | Kernel-owned copy of normalised memory regions |
| `rsdp` / `efi_system_table` | HHDM virtual addresses of ACPI/EFI tables |

## 2. Memory region normalisation

Limine memory map types are mapped to `enum o0_mem_type` (`include/ozero/boot.h`):

| Limine type | o0_mem_type | Meaning |
|-------------|-------------|---------|
| USABLE | `O0_MEM_FREE` | Usable RAM, owned by the kernel |
| BOOTLOADER_RECLAIMABLE | `O0_MEM_RECLAIMABLE` | On loan until reclaim completes |
| EXECUTABLE_AND_MODULES | `O0_MEM_KERNEL` | Kernel image range |
| ACPI_RECLAIMABLE | `O0_MEM_ACPI_RECLAIM` | Reclaimable after ACPI tables copied |
| ACPI_NVS | `O0_MEM_ACPI_NVS` | ACPI non-volatile storage |
| RESERVED | `O0_MEM_RESERVED` | Reserved, not available |
| RESERVED_MAPPED | `O0_MEM_RESERVED_MAPPED` | Reserved, already mapped |
| BAD_MEMORY | `O0_MEM_BAD` | Bad memory |
| FRAMEBUFFER | `O0_MEM_FRAMEBUFFER` | Framebuffer region |

## 3. Reclaim rules

Bootloader-reclaimable memory (`O0_MEM_RECLAIMABLE`) is loaned to the kernel
during boot and must be released back or reclaimed before the bootloader
expects it back.

### 3.1 Reclaim checklist (must all pass before release)

- [ ] The kernel has finished reading all Limine responses it needs.
- [ ] The `o0_boot_info` structure and all kernel-owned copies (memmap, CPU info)
      are in kernel-owned memory, not in bootloader-reclaimable regions.
- [ ] The physical page-frame allocator (`o0_pmm_*`) is initialised and
      tracking all usable pages, including reclaimable ones as reserved.
- [ ] A safe mapping exists for any data that must survive the release.
- [ ] The bootloader's `limine_reclaimable_memory_request` response (if used)
      has been checked for the bootloader's expectations.

### 3.2 Release

`o0_pmm_release_bootloader_memory()` releases reclaimable regions back to the
allocator once the checklist passes. Currently returns 0 (not implemented) —
Stage 2 keeps reclaimable regions reserved until the checklist is documented
and verified.

## 4. Higher-half direct map (HHDM)

`hhdm_offset` enables `virt = phys + hhdm` conversion for HHDM-mapped regions.
Use `o0_hhdm_to_virt()` for a checked conversion that validates range and
overflow. Direct pointer casts from physical addresses are unsafe and
prohibited.

## 5. Page 0

Page 0 (physical address 0) is never returned as a valid allocation by
`o0_pmm_alloc_pages()`. A null `o0_paddr_t` (0) always means "no allocation".
