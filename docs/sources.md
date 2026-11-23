# Source index

Citation keys used in the Stage 0 documents. Facts attributed to Limine were checked
against the pinned protocol revision below. UEFI and ACPI section numbers were checked
against the HTML editions at the listed URLs on 2026-11-23.

Intel SDM section numbers in the source-to-contract table are taken from the **2018
combined Vol. 3A edition** mirrored at
<https://pdos.csail.mit.edu/6.828/2018/readings/ia32/IA32-3A.pdf>, because the current
Intel PDF download is bot-blocked from this environment. Chapter and section titles are
the stable anchor; numbers shift between editions as chapters are inserted. Where a
citation is only valid for the current edition (notably 5-level paging, which the 2018
mirror predates), it is marked as such.

## [SDM] Intel 64 and IA-32 Architectures Software Developer's Manual

Index: <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>

| Key | Volume | Chapters used |
|---|---|---|
| [SDM-1] | Vol. 1, Basic Architecture | Basic execution environment, supported memory models |
| [SDM-2A] | Vol. 2A, Instruction Set Reference A-L | `CPUID`, `MOV` to/from control registers, `RDMSR`/`WRMSR`, `XGETBV`, `SYSCALL`/`SYSRET`, `IRET` |
| [SDM-3A] | Vol. 3A, System Programming Guide Part 1 | "System Architecture Overview" (§2.3 EFLAGS, §2.4 memory-management registers, §2.5 control registers), "Protected-Mode Memory Management" (§3.4.5 segment descriptors, §3.5.1 descriptor tables, §3.6 paging overview, §3.10 PAE paging in IA-32e), "Protection" (§4.5 privilege levels, §4.13 execute-disable), "Interrupt and Exception Handling" (§5.10 IDT, §5.14.5 IST), "Task Management" (§6.2.1 TSS), "Advanced Programmable Interrupt Controller (APIC)" (§8.4 local APIC, §8.5.4 APIC timer, §8.6 IPIs), "Processor Management and Initialization" (§9.6 SSE init, §9.8.5 IA-32e init), "Memory Cache Control" (§10.11 MTRRs, §10.12 PAT) |
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
deprecated. Base revision 6 differs from revision 5 only for aarch64, riscv64 and
loongarch64; **it adds no new x86-64 entry-state guarantees**. The x86-64 guarantees
cited in the boot contract are revision-5 guarantees that remain in force when revision 6
is requested.

Pinned artefacts (SHA-256, computed 2026-11-23):

| Artefact | Revision | SHA-256 |
|---|---|---|
| `PROTOCOL.md` | protocol `877c2d4c` | `3e702cd9dbfa2ead76eab5b34479fbc507b031d599b56fb5eb1db743cfd08f68` |
| `include/limine.h` | protocol `877c2d4c` | `bd2fa78d45353725952d7f9bd1fec17e3e19e4721af95f961d4d6b9285a1ffeb` |
| `limine-binary.tar.xz` | bootloader `v12.11.1` | `493312624a0a7a3afadcf8e9993f5418a87258d6b54f9d7ebb914037fe089220` |
| `BOOTX64.EFI` | bootloader `v12.11.1` | `4ddecdc6df2825ae5ce9ade19e48a5145dc753039c2a1ff0c39f9995a3646592` |
| `limine-bios.sys` | bootloader `v12.11.1` | `dfa6878acf5029509d7ac898667529716684e196d0f6f9d4880bb99cdda6d051` |
| `limine-bios-cd.bin` | bootloader `v12.11.1` | `b7e71d2162d1656225e39e4cb9e2a13f368a5d3c03c7303b7dae195bfedfafdc` |
| `limine-uefi-cd.bin` | bootloader `v12.11.1` | `e400e9b29aed90131f1bc1f2c4b6d9a8b58426e3318d6cdb7f160094427a2e34` |

Consistency checks performed on the downloaded release:

- `common/protos/limine.c` in `v12.11.1` defines `SUPPORTED_BASE_REVISION 6`.
- The vendored `limine-protocol/PROTOCOL.md` and `limine-protocol/include/limine.h` in the
  `v12.11.1` source tarball have the same SHA-256 as the pinned protocol revision above.
- The release has no Linux `limine` host tool in `limine-binary.tar.xz`; A4 must build the
  tool from the pinned source tarball or pin the host-tool package separately.


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
