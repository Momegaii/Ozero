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
| `kernel_phys` / `kernel_virt` / `kernel_phys_size` | Kernel load location; `kernel_phys` is resolved by walking the bootloader's page tables for `_kernel_start` |
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
during boot and must be returned or accounted for before the bootloader
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
allocator once the checklist passes. It is called from the boot sequence after
SMP bring-up and TLB shootdown, once `o0_boot_info`, ACPI tables and the SMP
response have been copied into kernel-owned memory. Reclaimed regions are
cleared from the PMM reservation and returned to the free pool. ACPI-reclaimable
memory is not released. Calling it twice is a no-op for already-released
regions. Returns the number of bytes released, or 0 if preconditions are unmet.

## 4. Higher-half direct map (HHDM)

`hhdm_offset` enables `virt = phys + hhdm` conversion for HHDM-mapped regions.
Use `o0_hhdm_to_virt()` for a checked conversion that validates range and
overflow. Direct pointer casts from physical addresses are unsafe and
prohibited.

### 4.1 Direct-map coverage

When the kernel activates its own page tables (`o0_paging_load()`), the direct
map is rebuilt over physical `[0, 4 GiB)` at `hhdm_offset` using 2 MiB pages. If
the bootloader reports a zero offset the same window is identity-mapped instead.
Because the map is built from 2 MiB pages, `hhdm_offset` is the 2 MiB-aligned
bootloader offset; `virt = phys + hhdm_offset` therefore holds for every address
the kernel maps, including physical 0.

Consequences that callers must respect:

- A physical address at or above 4 GiB has no direct-map virtual address;
  `o0_phys_to_virt()` returns NULL and `o0_hhdm_to_virt()` returns `O0_ERANGE`.
  The allocator can still hand out such frames on large machines, so every
  conversion must be checked rather than assumed.
- The kernel image is mapped separately at its linked high-half address, so
  kernel code, data and BSS never depend on the direct-map window.
- Extending the window is a deliberate change to `O0_DIRECT_MAP_LIMIT` in
  `kernel/arch/x86_64/paging.c` plus a larger page-table pool budget.

## 5. Page 0

Page 0 (physical address 0) is never returned as a valid allocation by
`o0_pmm_alloc_pages()`. A null `o0_paddr_t` (0) always means "no allocation".
