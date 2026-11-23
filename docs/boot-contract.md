# Boot contract (Stage 0)

Status: draft, pending director approval of D1. Normative words: **must**, **must not**,
**should**. This contract is written against Limine base revision 6, the newest revision at
protocol commit `877c2d4c` (see `sources.md`). If the bootloader choice changes, this
document must be re-expressed, not patched.

This contract is testable in QEMU before real hardware; the assertions are in §12 and the
test plan is in `stage1-plan.md`.

## 1. Scope

This document defines:

- the exact machine state the kernel may rely on at entry;
- the boot information the kernel consumes and where it lives;
- who owns each class of physical memory after handoff;
- the ordered handoff steps and the preconditions for reclaiming bootloader memory.

It does not define the kernel's internal allocator, scheduler or driver model.

## 2. Boot path decision

### 2.1 Options compared

| Criterion | UEFI + Limine (recommended) | Custom UEFI loader |
|---|---|---|
| Work before first kernel instruction | Configure Limine, link kernel at the required base | Implement PE/ELF loading, `GetMemoryMap`/`ExitBootServices` handshake, page tables, stack, RSDP discovery |
| `ExitBootServices` correctness | Handled by Limine, including `MapKey` retry and event groups [UEFI-7.4.6] | A1 must implement and prove it |
| Memory map normalisation | Usable / reclaimable / ACPI / framebuffer / reserved-mapped types [LIMINE-MEMMAP] | A1 must classify EFI types itself |
| Higher-half mappings and HHDM | Provided; offset discoverable [LIMINE-HHDM] [LIMINE-LAYOUT] | A1 must build them |
| SMP startup | APs parked in long mode; kernel publishes entry [LIMINE-MP] | A1 must write and debug an INIT-SIPI-SIPI trampoline |
| ACPI discovery | RSDP provided [LIMINE-RSDP] | A1 must scan firmware tables |
| Dependency and trust surface | Third-party bootloader in the TCB; version pinned | Code owned by Ozero, larger attack surface to get right |
| Failure modes | Protocol misread, bootloader bug | Bootloader bug plus kernel bug, harder to separate |
| Stage 0/1 cost | Low | High, duplicates working code |

### 2.2 Recommendation

Adopt **UEFI firmware + Limine base revision 6 + non-relocatable higher-half kernel**.
Rationale: the hard parts of firmware handoff are exactly the parts Limine already
specifies and implements, and the protocol gives the kernel a precise, versioned entry
state. A custom loader is rejected for Stage 1 because it duplicates security-sensitive
code with no requirement that Limine cannot meet. [UEFI-7.2.3] [UEFI-7.4.6]
[LIMINE-STATE] [LIMINE-MEMMAP]

Status: **recommended, pending director approval.** The kernel must refuse to boot if the
base revision tag was not honoured (`tag[2] != 0`), rather than guess.

### 2.3 Firmware path

UEFI is the supported path. A hybrid image may also be produced for test convenience
(U7), but no kernel behaviour may depend on the firmware type beyond the presence or
absence of the RSDP and EFI system table. The firmware type is recorded in boot info.
[LIMINE-FW]

## 3. Entry state (x86-64, base revision 6)

The kernel may rely on exactly this state. Anything not listed is undefined and must be
established by the kernel. [LIMINE-STATE] [LIMINE-BASE]

### 3.1 Processor mode and control registers

| Item | Guaranteed state | Consequence for the kernel |
|---|---|---|
| Mode | IA-32e long mode, CPL 0 | No protected-mode transition code needed |
| `CR0` | `PE`, `ET`, `WP`, `PG` set; all other bits clear | `EM=0`, `TS=0`, `MP=0`, `NE=0`; the kernel must set `MP=1`, `NE=1` before x87/SSE |
| `CR4` | `PAE` set; `LA57` set only if 5-level was requested and is available; all other bits clear | `OSFXSR=0`, `OSXMMEXCPT=0`, `OSXSAVE=0`, `PGE=0`, `SMEP=0`, `SMAP=0`, `PCIDE=0`, `FSGSBASE=0`, `UMIP=0`; the kernel enables what it needs |
| `EFER` | `LME`, `LMA`, `NXE` set (`NXE` only if NX is available) | `SCE=0`; `SYSCALL` is not enabled at entry |
| Paging | Enabled; 4-level by default, 5-level only if requested | Kernel page tables must be compatible with the active mode |
| `RFLAGS` | `0x00000002`; `IF=0`, `DF=0`, `VM=0` | Interrupts are off; no `cld` needed before string ops, but the kernel should not rely on it after enabling interrupts |

### 3.2 General registers and stack

| Item | Guaranteed state |
|---|---|
| `RIP` | Entry point from the executable, or the Entry Point feature value [LIMINE-ENTRY] |
| `CS` | `0x28`, 64-bit code descriptor, base 0 |
| `DS`, `ES`, `SS`, `FS`, `GS` | `0x30`, 64-bit data descriptor, base 0 |
| `FS`/`GS` base | 0 |
| `RAX`–`R15` | All zero |
| `RSP` | Top of a stack of at least 64 KiB (or the requested Stack Size), in bootloader-reclaimable memory [LIMINE-STACK] |
| Return address | Invalid value `0` pushed on the stack |
| `RFLAGS.DF` | Clear |

The initial stack is a loan. The kernel must switch to a kernel-owned stack before it
reclaims bootloader memory, and must never return from the entry point.

### 3.3 Descriptor tables and task state

| Item | Guaranteed state |
|---|---|
| `GDTR` | Points to a bootloader GDT in bootloader-reclaimable memory with null, 16-bit code/data, 32-bit code/data, and 64-bit code/data descriptors at offsets 0–6 |
| `IDTR` | Base 0, limit 0; no IDT present |
| `TR` | Base 0, limit 0; no TSS present |
| `LDTR` | Null selector; no LDT present |
| Segment limits | Not enforced in 64-bit mode; `CS`/`DS`/`ES`/`SS` bases forced to 0 |

The kernel must install its own GDT, TSS and IDT before it enables interrupts, uses IST
stacks, or reclaims the bootloader GDT.

### 3.4 Platform state

| Item | Guaranteed state |
|---|---|
| A20 | Open |
| Legacy PIC | All IRQs masked |
| I/O APIC | Fixed and lowest-priority redirection entries masked; NMI and ExtINT entries also masked at revision ≥ 5 |
| Local APIC | Enabled and software-enabled; `SVR = 0x1FF`, `TPR = 0`, all applicable LVT entries masked; NMI LINT entries configured from MADT and masked |
| x2APIC | Enabled only if requested through the MP feature flag and supported; the response reports which mode is active |
| IOMMU | Intel VT-d DMA translation and interrupt remapping disabled; AMD-Vi disabled |
| Boot services | Exited if booted by EFI |
| Interrupt controllers | The kernel must program routing before unmasking any line |

### 3.5 State the kernel must not assume

- **Floating point and SIMD state is not specified.** `CR0.MP=0` at entry and `CR4.OSFXSR=0`.
  The entry translation unit must be built with `-mgeneral-regs-only` (or equivalent), or
  must enable SSE before any compiler-generated SSE instruction executes. A compiler that
  emits SSE for a struct copy will fault otherwise.
- **`MXCSR`, x87 control word and XCR0 contents are not specified.** Initialise them.
- **No IDT, no TSS, no IST stack.** Any exception before the kernel installs its IDT is a
  triple fault with no diagnostics.
- **`PAT` contents are not specified for the kernel to change**, but Limine programs a
  defined PAT layout when PAT is supported: `PAT0=WB`, `PAT5=WC`; MTRRs are left as
  firmware set them. [LIMINE-CACHE] The kernel must not reprogram MTRRs without a full
  cache-coherency plan.
- **The HHDM offset is not fixed.** Discover it and never hard-code it. [LIMINE-HHDM]
- **The bootloader GDT, stack, page tables, request responses and MP structures are in
  bootloader-reclaimable memory.** They disappear when the kernel frees that memory.

## 4. Boot information

The kernel copies everything it needs into kernel-owned memory during early init. The
canonical structure is `struct o0_boot_info` in `include/o0/boot.h`.

| Field group | Contents | Source | Lifetime requirement |
|---|---|---|---|
| Identity | magic, version, structure size, base revision requested and loaded | Base revision tag | Copy immediately |
| Firmware | firmware type (BIOS/EFI32/EFI64/SBI) | Firmware Type feature | Copy |
| Memory | HHDM offset; normalised memory map array and count | HHDM and Memory Map features | Copy map before PMM init |
| Kernel | physical base and virtual base of the image | Executable Address feature | Copy |
| Modules | none in Stage 1 | Module feature (not requested) | n/a |
| CPUs | CPU count, BSP LAPIC ID, x2APIC flag, per-CPU ACPI UID and LAPIC ID | MP feature | Copy before AP takeover |
| ACPI | RSDP address (HHDM virtual at revision ≥ 4) | RSDP feature | Copy pointer; copy tables later |
| EFI | system table address, EFI memory map pointer/size/descriptor size/version | EFI System Table and EFI Memory Map features | Copy descriptors if runtime services are ever enabled |
| Time | TSC frequency in Hz, or 0 if unknown | TSC Frequency feature | Copy; validate with a second source before trusting |
| Console | framebuffer count and first framebuffer description, if present | Framebuffer feature | Copy; Stage 1 uses serial only |

Population rules:

1. Every Limine response pointer is validated for non-null before use. A missing response
   for a requested feature is a boot failure, except RSDP (ACPI may be absent) and TSC
   frequency (fall back to another clocksource).
2. Pointers inside responses are converted to kernel-owned copies before the reclaim
   phase. The copied map is the authoritative one; the Limine arrays are not.
3. `struct o0_boot_info` is versioned. The kernel accepts an older structure only through
   an explicit compatibility path, never by reinterpreting fields.
4. Boot info is read-only after early init, except for the reclaim state of memory regions.

## 5. Memory map normalisation

Limine guarantees that entries are sorted by base address, that usable and
bootloader-reclaimable entries are 4 KiB aligned in base and length, and that those two
types do not overlap any other entry. Other types are not guaranteed alignment or
non-overlap. [LIMINE-MEMMAP]

| Limine type | `o0_mem_type` | HHDM mapped at rev 6 | Writable by kernel | Allocator use |
|---|---|---|---|---|
| `USABLE` (0) | `O0_MEM_FREE` | Yes | Yes | Immediately free |
| `RESERVED` (1) | `O0_MEM_RESERVED` | **No** | No | Never |
| `ACPI_RECLAIMABLE` (2) | `O0_MEM_ACPI_RECLAIM` | Yes | Only after tables are copied | Later, via explicit reclaim |
| `ACPI_NVS` (3) | `O0_MEM_ACPI_NVS` | Yes | No | Never |
| `BAD_MEMORY` (4) | `O0_MEM_BAD` | **No** | No | Never |
| `BOOTLOADER_RECLAIMABLE` (5) | `O0_MEM_RECLAIMABLE` | Yes | Only after §8 checklist | After reclaim phase |
| `EXECUTABLE_AND_MODULES` (6) | `O0_MEM_KERNEL` | Yes | Kernel image only | Never |
| `FRAMEBUFFER` (7) | `O0_MEM_FRAMEBUFFER` | Yes, write-combining | Only through the framebuffer API | Never |
| `RESERVED_MAPPED` (8) | `O0_MEM_RESERVED_MAPPED` | Yes | No | Never |

Rules:

- HHDM coverage follows the protocol: usable, bootloader-reclaimable,
  executable-and-modules, framebuffer, reserved-mapped, ACPI reclaimable and ACPI NVS are
  mapped; plain reserved and bad memory are not. [LIMINE-LAYOUT] The kernel must never
  dereference a physical address through the HHDM unless its type is in the mapped set.
- `EXECUTABLE_AND_MODULES` and `FRAMEBUFFER` entries are illustrative, not authoritative.
  The kernel image range comes from the Executable Address feature; the framebuffer comes
  from the Framebuffer feature. [LIMINE-MEMMAP] [LIMINE-EXECADDR]
- HHDM mappings use a minimum 4 KiB page and may overshoot an unaligned region boundary to
  cover it. A mapped page therefore does not prove ownership of every byte in it. Use the
  memory map, not the mapping, to decide what may be allocated. [LIMINE-LAYOUT]
- Framebuffer pages use write-combining (`PAT5`). Do not map them again with a conflicting
  cache attribute in the same page.

## 6. Memory ownership

Ownership transfers at handoff, with one loan:

- **Kernel-owned:** all `USABLE` memory.
- **On loan from the bootloader:** `BOOTLOADER_RECLAIMABLE`, until the reclaim checklist
  passes. This includes the initial stack, the bootloader GDT, the bootloader page tables,
  Limine request/response storage, the MP structure array, and parked AP stacks.
- **Firmware-owned, never allocated:** `RESERVED`, `RESERVED_MAPPED`, `ACPI_NVS`,
  `BAD_MEMORY`.
- **Kernel image:** the physical range reported by the Executable Address feature.
- **Framebuffer and MMIO:** device memory, mapped but never freed.

Two-phase reclaim is mandatory: during PMM init, all reclaimable memory is reserved and
unavailable to the allocator. A later explicit phase releases it only after the §8
checklist passes. This prevents an early allocation from stealing the running stack or the
parked APs' memory.

## 7. Handoff sequence

Each step is ordered and testable. Steps H0–H2 are assembly or `-mgeneral-regs-only` C;
later steps may use normal kernel C.

| Step | Action | Exit condition |
|---|---|---|
| H0 | Entry stub: establish a known stack alignment, disable nothing (interrupts already off), record the bootloader stack range as reclaimable | Stub reaches H1 without faulting |
| H1 | Initialise COM1 by port I/O and emit a banner with base revision and firmware type | Serial output observed in QEMU |
| H2 | Enable FPU/SSE: `CR0.MP=1`, `CR0.NE=1`, `CR0.EM=0`, `CR0.TS=0`, `CR4.OSFXSR=1`, `CR4.OSXMMEXCPT=1`; `FNINIT`; set a known `MXCSR`; if XSAVE is used, `CR4.OSXSAVE=1` and a validated `XCR0` | A C function compiled with SSE runs correctly |
| H3 | Copy all Limine responses, the memory map, the MP CPU list, the EFI memory map and the RSDP pointer into a static boot arena in kernel BSS | Boot info is self-contained; no Limine pointer is dereferenced afterwards |
| H4 | Install kernel GDT (null, kernel code, kernel data, user code, user data, TSS), build a TSS with IST1–IST3 stacks, load `GDTR`, reload segment registers, `ltr` | `sgdt`/`str` readback matches; no fault |
| H5 | Install kernel IDT with trap gates, load `IDTR`, self-test with `int3` and a recoverable `ud2` | Handler prints vector and returns; counters increment |
| H6 | Initialise PMM from the copied map: default-reserve everything, add `USABLE` as free, reserve the kernel image, boot arena, firmware regions, and all `BOOTLOADER_RECLAIMABLE` as pending | Free page count is plausible and matches a hand calculation for a small QEMU memory size |
| H7 | Build kernel page tables: kernel image with W^X, HHDM for every HHDM-covered type, per-CPU and IST stacks, MMIO/framebuffer as needed; load `CR3`; verify by reading a known HHDM address | Readback matches; old page tables no longer referenced |
| H8 | Switch to a kernel-allocated stack; record the bootloader stack as reclaimable | Stack pointer is outside bootloader-reclaimable memory |
| H9 | Take over APs: per-CPU area and kernel stack per AP, publish entry with a release store to `goto_address`, wait for an acknowledgement from every AP with a timeout [LIMINE-MP] | `online == cpu_count` or a documented timeout failure |
| H10 | Validate and copy ACPI tables (RSDP → XSDT → MADT/FADT); record IOAPIC GSIs and overrides | Checksums valid; CPU count agrees with the MP feature |
| H11 | Release bootloader-reclaimable memory through the §8 checklist | Allocation from the released range succeeds and patterns verify |
| H12 | Enable interrupts, start the timer, run the Stage 1 self-test | `[o0] stage1: PASS` on serial |

## 8. Reclaim preconditions

All of the following must be true before a single bootloader-reclaimable page is handed to
the allocator:

1. Kernel GDT, TSS, IST stacks and IDT are installed and self-tested.
2. The kernel is running on its own page tables (`CR3`), and no code path still uses the
   bootloader page tables.
3. The BSP is on a kernel-owned stack.
4. Every AP has acknowledged takeover and is running on a kernel-owned stack. A parked AP
   still executes bootloader code on a bootloader stack; freeing it is a use-after-free
   that manifests as a random AP triple fault.
5. All Limine responses, the memory map, the MP CPU list, the EFI memory map and the RSDP
   pointer have been copied, or are provably no longer needed.
6. No interrupt handler, timer callback or console path references bootloader memory.
7. Any TPM event log, SMBIOS data or other optional bootloader-provided buffer that will
   be used has been copied.

The reclaim phase is reversible in one respect only: if a precondition fails, the pages
stay reserved. There is no partial reclaim of a structure that is still in use.

## 9. SMP handoff

- The MP feature is requested even on a uniprocessor system; a response containing only
  the BSP is valid. [LIMINE-MP]
- Limine boots APs into the same mode as the BSP and parks them. Each `limine_mp_info`
  has a `goto_address` that the kernel publishes atomically. The kernel must write
  `extra_argument` and any per-CPU state before the release store. [LIMINE-MP]
- The AP entry receives a pointer to its `limine_mp_info` in `RDI`; it must immediately
  switch to its kernel stack and acknowledge takeover before touching shared state.
- AP stacks supplied by Limine follow the Stack Size feature and live in
  bootloader-reclaimable memory. They are loans, not kernel stacks.
- `reserved` and `reserved1` fields in `limine_mp_info` belong to the bootloader and must
  not be modified. [LIMINE-MP]
- MTRRs on APs are synchronised by the bootloader; the kernel must not assume it can
  change MTRRs per-CPU without a coherency plan. [LIMINE-MP] [SDM-3A] "Memory Cache
  Control".
- The kernel maps ACPI UIDs to LAPIC IDs from the MP list and cross-checks against MADT.
  A mismatch is logged, not fatal, unless it prevents IPI delivery.

## 10. ACPI and EFI handoff

### ACPI

- Use the RSDP from the RSDP feature. At base revision ≥ 4 it is a virtual address in the
  HHDM; at revision 3 it is physical. [LIMINE-RSDP] [LIMINE-BASE]
- Validate the RSDP signature, checksum, revision and, for revision 2+, the extended
  checksum before following the XSDT. [ACPI-5.2.5]
- Validate every table header (signature, length, checksum) before reading entries.
  [ACPI-5.2.8] [ACPI-5.2.12]
- MADT is the authority for CPU LAPIC IDs, IOAPIC IDs, GSI base and interrupt source
  overrides. FADT supplies legacy fallback values. DSDT/AML is not evaluated in Stage 1.
  [ACPI-5.2.12] [ACPI-5.2.9]
- ACPI tables are guaranteed to be mapped inside ACPI reclaimable, ACPI NVS or
  reserved-mapped regions at revision ≥ 4. Do not copy them to a new address unless the
  original region is about to be reclaimed. [LIMINE-MEMMAP]

### EFI

- Boot services are gone. No `BS->` call is legal after handoff. [UEFI-7.4.6]
- Runtime services remain callable in physical mode, but the kernel must not call them in
  Stage 1 (D7). The system table and EFI memory map are recorded for a future decision.
- If runtime services are enabled later, `SetVirtualAddressMap` must be called from
  physical mode with descriptors whose virtual addresses are the HHDM addresses, i.e. the
  EFI memory map with the HHDM offset subtracted as documented by Limine, and only then
  may virtual pointers be used. [UEFI-8.4.1] [LIMINE-EFIMM]
- The EFI system table and runtime regions are mapped as reserved-mapped memory at
  revision ≥ 5; they are never allocatable. [LIMINE-BASE]

## 11. Refuse-to-boot conditions

The kernel halts with a specific serial message and a non-zero QEMU exit code when:

| Condition | Reason |
|---|---|
| Base revision tag not honoured (`tag[2] != 0`) | The bootloader did not implement the requested contract |
| Long mode, PAE or NX not available | The baseline is not met; W^X cannot be enforced |
| HHDM response missing | No safe physical-to-virtual mapping |
| Memory map response missing or empty | No allocator input |
| MP response missing when requested | AP state is unknown; reclaim would be unsafe |
| Kernel image not fully covered by the memory map | The image may be overwritten |
| Usable memory below the configured floor | Cannot boot the kernel |
| Memory map not sorted, or usable/reclaimable regions overlap | Violates the protocol guarantee; do not guess |
| IDT/GDT self-test fails | The kernel cannot handle its own faults |

Missing RSDP, missing TSC frequency or an invalid ACPI table are degraded conditions, not
fatal: log, fall back to single-CPU/no-ACPI and a non-TSC clocksource, and continue.

## 12. Testable assertions (QEMU)

| ID | Assertion | Expected serial evidence |
|---|---|---|
| B1 | Base revision honoured and firmware type known | `[o0] boot: limine base=6 loaded=6 fw=uefi64` |
| B2 | HHDM offset discovered, kernel image located | `[o0] boot: hhdm=0x... kernel_phys=0x... kernel_virt=0xffffffff80000000` |
| B3 | Memory map normalised and plausible | `[o0] memmap: entries=N usable=XMiB reclaimable=YMiB` |
| B4 | CPU baseline met and optional features reported | `[o0] cpu: nx=1 ... smep=1 smap=1 la57=0` |
| B5 | GDT/TSS/IDT installed | `[o0] gdt: ok tss=0x... ist=ok` / `[o0] idt: ok vectors=256` |
| B6 | Exception path works | `[o0] selftest: int3=ok ud2=ok` |
| B7 | Own page tables active | `[o0] vmm: cr3=0x... hhdm_readback=ok` |
| B8 | APs taken over | `[o0] smp: bsp=0 cpus=4 online=4` |
| B9 | ACPI topology agrees with MP list | `[o0] acpi: madt_cpus=4 ioapics=1 overrides=2` |
| B10 | Reclaim completed safely | `[o0] reclaim: released=YMiB free=ZMiB` |
| B11 | No reserved or bad memory allocated | `[o0] pmm: reserved_violations=0` |
| B12 | Stage 1 self-test passes | `[o0] stage1: PASS` |

Assertions B1–B7 and B10–B12 are single-CPU tests. B8–B9 require `-smp 2` and `-smp 4`.

## 13. Open questions

- Is `SetVirtualAddressMap` ever required for Ozero, or is runtime services support out of
  scope permanently? (U6)
- Does the product require legacy BIOS boot, or is a UEFI-only image acceptable? (U7)
- Which QEMU CPU models define the minimum test envelope (`qemu64`, `max`, host
  passthrough)? This determines which optional-feature paths are exercised. (A4)
- Is an IOMMU required before untrusted drivers exist? Limine disables VT-d/AMD-Vi, so
  Stage 1 has no DMA isolation. (A7, U4)
