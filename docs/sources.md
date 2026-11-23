# Source index

Citation keys used in the Stage 0 documents. Facts attributed to Limine were checked
against the pinned protocol revision below. UEFI and ACPI section numbers were checked
against the HTML editions at the listed URLs on 2026-11-23. Intel SDM references are by
volume and chapter title because chapter numbering shifts between editions.

## [SDM] Intel 64 and IA-32 Architectures Software Developer's Manual

Index: <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>

| Key | Volume | Chapters used |
|---|---|---|
| [SDM-1] | Vol. 1, Basic Architecture | Basic execution environment, supported memory models |
| [SDM-2A] | Vol. 2A, Instruction Set Reference A-L | `CPUID`, `MOV` to/from control registers, `RDMSR`/`WRMSR`, `XGETBV`, `SYSCALL`/`SYSRET`, `IRET` |
| [SDM-3A] | Vol. 3A, System Programming Guide Part 1 | "System Architecture Overview", "Paging", "Protection", "Interrupt and Exception Handling", "Task Management", "Multiple-Processor Management", "Advanced Programmable Interrupt Controller (APIC)", "Memory Cache Control" |
| [SDM-4] | Vol. 4, Model-Specific Registers | `IA32_EFER`, `IA32_APIC_BASE`, `IA32_PAT`, `IA32_MTRR_*`, `IA32_GS_BASE`, `IA32_KERNEL_GS_BASE`, `IA32_STAR`, `IA32_LSTAR`, `IA32_FMASK`, `IA32_TSC_AUX`, `IA32_X2APIC_*` |

Section-level fact used: Limine's MP feature notes that the bootloader synchronises AP
MTRRs as required by Intel SDM Vol. 3A, 12.11.5.

## [UEFI] Unified Extensible Firmware Interface Specification 2.11

Index: <https://uefi.org/specs/UEFI/2.11/>

| Key | Section | Subject |
|---|---|---|
| [UEFI-7.2.3] | 7.2.3 `EFI_BOOT_SERVICES.GetMemoryMap()` | Memory map, `MapKey`, descriptor format |
| [UEFI-7.4.6] | 7.4.6 `EFI_BOOT_SERVICES.ExitBootServices()` | Boot-services teardown, memory ownership, `MapKey` race |
| [UEFI-8.1] | 8.1 Runtime Services Rules and Restrictions | Runtime services remain callable after boot services exit |
| [UEFI-8.4.1] | 8.4.1 `SetVirtualAddressMap()` | Physical-to-virtual transition for runtime services |

Primary pages:
<https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html>,
<https://uefi.org/specs/UEFI/2.11/08_Services_Runtime_Services.html>

## [ACPI] Advanced Configuration and Power Interface Specification 6.6

Index: <https://uefi.org/specs/ACPI/6.6/>

| Key | Section | Table |
|---|---|---|
| [ACPI-5.2.5] | 5.2.5 | Root System Description Pointer (RSDP), incl. 5.2.5.2 UEFI discovery |
| [ACPI-5.2.7] | 5.2.7 | Root System Description Table (RSDT) |
| [ACPI-5.2.8] | 5.2.8 | Extended System Description Table (XSDT) |
| [ACPI-5.2.9] | 5.2.9 | Fixed ACPI Description Table (FADT) |
| [ACPI-5.2.11.1] | 5.2.11.1 | Differentiated System Description Table (DSDT) |
| [ACPI-5.2.12] | 5.2.12 | Multiple APIC Description Table (MADT) |
| [ACPI-5.2.16] | 5.2.16 | System Resource Affinity Table (SRAT) |

Primary page: <https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html>

## [LIMINE] The Limine Boot Protocol

Repository: <https://github.com/limine-bootloader/limine-protocol>

Pinned revision: `877c2d4c50e170575f8b8e7fbf7b8fa89b1dc157` (2026-11-14),
retrieved 2026-11-23 from
<https://raw.githubusercontent.com/limine-bootloader/limine-protocol/trunk/PROTOCOL.md>.
At that revision the protocol specifies base revisions 0 through 6; 0 through 5 are
deprecated.

| Key | Section |
|---|---|
| [LIMINE-BASE] | Base Revisions, Base Revision 6 |
| [LIMINE-LAYOUT] | Memory Layout at Entry |
| [LIMINE-STATE] | Machine State at Entry, x86-64 |
| [LIMINE-CACHE] | Caching, x86-64 |
| [LIMINE-FW] | Firmware Type Feature |
| [LIMINE-STACK] | Stack Size Feature |
| [LIMINE-HHDM] | HHDM (Higher Half Direct Map) Feature |
| [LIMINE-PAGING] | Paging Mode Feature |
| [LIMINE-MP] | MP (Multiprocessor) Feature |
| [LIMINE-MEMMAP] | Memory Map Feature |
| [LIMINE-ENTRY] | Entry Point Feature |
| [LIMINE-EXECADDR] | Executable Address Feature |
| [LIMINE-RSDP] | RSDP Feature |
| [LIMINE-EFIST] | EFI System Table Feature |
| [LIMINE-EFIMM] | EFI Memory Map Feature |
| [LIMINE-TSC] | TSC (Timestamp Counter) Frequency Feature |

## [QEMU] QEMU System Emulation

<https://www.qemu.org/docs/master/system/>

Used for the Stage 1 test matrix only. Emulator success is not evidence of physical
hardware support.
