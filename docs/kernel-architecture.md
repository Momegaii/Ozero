# Ozero kernel architecture note (Stage 0)

Status: draft ready for review. This note defines responsibilities and contracts. It does
not approve implementation. Every decision marked *pending* requires director sign-off
before Stage 1 work begins.

Scope: x86-64 only. Architecture-specific code is isolated so that a future port is a
separate project; no other architecture is designed here.

## 1. Responsibilities

| Owned by A1 | Not owned by A1 |
|---|---|
| Kernel entry and early initialisation | Bootloader implementation (uses Limine, see D1) |
| CPU feature discovery and baseline policy | Final syscall ABI and Windows compatibility surface (A2) |
| GDT, IDT, TSS, exception and interrupt entry | Device-class drivers and their protocols (A3) |
| Physical and virtual memory interfaces | Repository, build and test integration (A4) |
| Thread, process and address-space primitives (Stage 2+) | Installer, packaging, update path |
| Kernel/user privilege boundary and user-copy rules | Firmware update, ACPI AML interpreter, power management |
| Generic driver-facing kernel services (MMIO, IRQ, DMA, I/O requests) | Filesystems, networking stacks |

Deliberately out of scope for Stage 0/1: every large subsystem implementation. Stage 0
delivers stable contracts; Stage 1 delivers a booting kernel with memory, interrupts and
a single ring-3 test thread.

## 2. Execution model and privilege boundary

- Two privilege levels only: ring 0 (kernel) and ring 3 (user). Rings 1 and 2 are unused
  and their descriptor entries are not populated. [SDM-3A] "Protection".
- The kernel executes in long mode with 4-level paging by default; 5-level paging is an
  opt-in boot mode, not a second code path in generic services (D2).
- User space occupies the lower canonical half; the kernel occupies the higher canonical
  half. The kernel image is linked at `0xffffffff80000000`, the Limine requirement for
  non-relocatable executables. [LIMINE-LAYOUT]
- The kernel is mapped in every address space. Stage 1 does not implement KPTI; whether a
  Meltdown-class threat model requires it is U1. User mappings carry the user bit; kernel
  mappings do not.
- Kernel code and read-only data are mapped read-only and non-executable data is NX. W^X
  is enforced through page permissions, not linker convention alone.
- SMEP and SMAP are enabled when the CPU reports them, and user copies use explicit
  `stac`/`clac` windows. If absent, the kernel relies on page permissions and careful
  pointer validation; this is weaker and is recorded as a degradation (see CPU matrix).
- `swapgs` and per-CPU data are used on every kernel entry. User-controlled values are
  never trusted before validation.
- Syscall entry: an `int 0x80` gate is implemented first because it needs no MSR setup;
  the `SYSCALL`/`SYSRET` fast path is added after the trap path is proven. Vector numbers
  are reserved now so the switch does not change the ABI sketch. [SDM-2A] `SYSCALL`.
- User pointers are validated for canonical form, range, overflow and user-half
  membership before any access. Failure returns `O0_EFAULT`; it never faults in kernel
  mode on a bad user address.

## 3. Subsystem map

Interfaces are sketches in `include/ozero/`. Header names are stable; function signatures
are not frozen.

| Subsystem | Responsibility | Public header |
|---|---|---|
| `arch/x86_64/entry` | Entry stub, boot-info capture, stack switch, FPU/SSE enable | `boot.h` |
| `arch/x86_64/cpu` | CPUID/MSR access, feature table, control-register policy | `cpu.h` |
| `arch/x86_64/gdt` | GDT, TSS, IST stacks, per-CPU segment setup | `cpu.h` |
| `arch/x86_64/idt` | IDT, trap/IRQ stubs, exception decode and reporting | `irq.h` |
| `arch/x86_64/apic` | Local APIC/x2APIC, IOAPIC routing, IPIs, timer | `irq.h` |
| `arch/x86_64/paging` | Page-table construction, PCID, TLB maintenance | `mm.h` |
| `kernel/mm/pmm` | Physical page allocator over the normalised memory map | `mm.h` |
| `kernel/mm/vmm` | Address spaces, map/unmap/protect, kernel heap, vmalloc | `mm.h` |
| `kernel/mm/dma` | Generic DMA-capable allocation constrained by the caller | `mm.h` |
| `kernel/irq` | Vector allocation, GSI routing, handler registry | `irq.h` |
| `kernel/sched` | Threads, processes, context switch, idle, preemption | `thread.h` |
| `kernel/obj` | Object and handle tables, reference counting, wait primitives | `ipc.h` |
| `kernel/ipc` | Channels/messages built on objects | `ipc.h` |
| `kernel/drivers` | Device discovery, BAR mapping, IRQ/DMA plumbing, I/O requests | `driver.h` |
| `kernel/time` | Clocksource selection, monotonic time, timers | `cpu.h`, `irq.h` |
| `kernel/lib` | String/memory routines, printf, spinlocks, atomics, ranges | internal |

Dependency rule: `arch/x86_64` may depend on generic services; generic services may not
contain x86-64 instruction sequences or device-class assumptions. Architecture hooks are
declared in generic headers and implemented under `arch/`.

## 4. Repository layout (proposal; A4 owns build integration)

```
include/ozero/        public kernel headers (this Stage 0 deliverable)
arch/x86_64/       entry, cpu, gdt, idt, apic, paging, msr, tsc, serial
kernel/            mm, sched, obj, ipc, irq, drivers, time, lib
boot/              limine configuration and image assembly
tests/             host-side contract checks and QEMU boot tests
docs/              this documentation set
```

Build files are intentionally absent. A4 owns the build system; this layout is a
proposal to be ratified with A4 before Stage 1 coding.

## 5. Cross-agent contracts

### A2 (process/thread/address-space/object/syscall concepts)

A2 needs stable *concepts*, not final numbers:

- `o0_process_t` owns an address space and a handle table.
- `o0_thread_t` is a schedulable execution context inside exactly one process.
- `o0_as_t` is an opaque address space with map/unmap/protect operations.
- Handles are opaque 64-bit values, scoped to a process, carrying rights and a generation
  counter; they are not kernel pointers and not file descriptors.
- The syscall boundary exposes object operations, not Windows NT semantics. Final syscall
  numbers, calling convention and NT-compatible status codes are deferred to A2 and A4.

A2 review requested: confirm that these concepts cover the future process, thread,
address-space and handle requirements without freezing kernel internals. No ABI freeze
before A2 sign-off.

### A3 (driver-facing services)

A3 needs, and this note commits to providing:

- DMA-capable allocation described by a caller-supplied constraint record, never by a
  device-class assumption (D10).
- MMIO mapping with explicit cache attributes and a matching unmap.
- Interrupt registration by GSI with mask/unmask and MSI/MSI-X vector allocation.
- Device discovery callbacks from the bus layer.
- A generic asynchronous I/O request object for block-oriented devices.

Device-class protocols (storage, network, display, input) live in A3, not in these
headers. No GPU or Gen8+ assumptions appear in generic memory interfaces; the check is
automated in `tools/check-headers.sh`.

A1 interface review: signed with one recorded blocker — the DMA API has no IOMMU domain
because Stage 1 has no IOMMU. Details and per-item disposition are in
`docs/interface-draft.md` §11.

### A4 (repository, build, test)

- Header layout and include guards are stable so A4 can wire the build once.
- Interface changes go through review; no silent signature changes.
- A4 owns toolchain selection, image assembly and CI. `tools/check-headers.sh` is a
  provisional developer aid, not a CI definition.

## 6. Architectural decisions

Each decision lists the source that constrains it. All are pending director approval.

### D1. Boot path: UEFI firmware + Limine (base revision 6 requested) + kernel

Base revision 6 is requested, but its changes over revision 5 are non-x86 only; the x86-64
entry-state guarantees used here are revision-5 guarantees. [LIMINE-BASE]

Recommended over a custom UEFI loader. Limine already performs `ExitBootServices`, builds
the higher-half mappings and HHDM, normalises the firmware memory map into reclaimable
categories, supplies the RSDP and SMP startup, and defines the exact entry state.
[LIMINE-STATE] [LIMINE-MEMMAP] [LIMINE-HHDM] [UEFI-7.4.6]

A custom loader would have to reproduce the `GetMemoryMap`/`ExitBootServices` handshake,
including the `MapKey` race and retry rule, plus page-table construction, before writing
any kernel code. [UEFI-7.2.3] [UEFI-7.4.6] That is a large, security-sensitive duplicate
of working bootloader code with no Stage 0 requirement driving it.

Comparison and rejection criteria are in `docs/boot-contract.md` §2. Revisit only if
Limine's contract blocks a hard requirement.

### D2. Paging: 4-level baseline, 5-level opt-in

Request paging mode `min = 4-level`, `max = 5-level`, `preferred = 4-level` through the
Limine paging-mode feature, and record the mode actually enabled. [LIMINE-PAGING]
Generic services use pointer-width-independent types. 5-level support must not require a
second memory manager; it changes canonical-width checks and page-table depth only.

### D3. Kernel placement and direct map

Kernel image is non-relocatable at `0xffffffff80000000`, built with the kernel code model.
The HHDM offset is discovered at boot and stored in boot info; it is never hard-coded,
because the bootloader may choose or randomise it. [LIMINE-LAYOUT] [LIMINE-HHDM]

The kernel never assumes that a physical address is reachable through the HHDM unless the
region type is one Limine maps and the checked range predicate in boot contract §5.1
passes.

### D4. Interrupt model: APIC, no legacy PIC

Use the local APIC (x2APIC when the bootloader reports it enabled and the CPU supports it)
and the IOAPIC. Limine masks the legacy PIC and IOAPIC redirections before handoff, so the
kernel must program routing before unmasking anything. [LIMINE-STATE] [SDM-3A] "Advanced
Programmable Interrupt Controller (APIC)".

The IDT uses dedicated IST stacks for double fault, NMI and machine check. A double fault
that cannot use its IST stack is a guaranteed triple fault and loses all diagnostics.

### D5. SMP: Limine MP feature, kernel-controlled AP takeover

Request the MP feature so APs are parked deterministically in long mode with paging on.
Publish each AP's kernel entry with a release store to `goto_address` only after its
per-CPU state and kernel stack exist. Do not write an INIT-SIPI-SIPI trampoline in
Stage 1. [LIMINE-MP]

Consequence: parked APs execute bootloader trampoline code on bootloader stacks in
bootloader-reclaimable memory, so that memory cannot be reclaimed until takeover is
complete. This is a hard precondition in the boot contract §9.1–§9.3. Stage 1 has no AP
hotplug; after reclaim the bootloader trampoline is gone.

### D6. ACPI: RSDP → XSDT → MADT/FADT only

Use the Limine-provided RSDP (virtual through the HHDM for base revision ≥ 4), validate
checksums and lengths, then read XSDT, MADT and FADT. MADT supplies CPU and IOAPIC
topology and interrupt overrides; FADT supplies legacy fallback information. No AML
interpreter and no DSDT namespace evaluation in Stage 1. [ACPI-5.2.5] [ACPI-5.2.8]
[ACPI-5.2.12] [ACPI-5.2.9]

### D7. EFI runtime services: recorded, not used, in Stage 1

Limine exits boot services, so no boot service may be called. Runtime services remain
callable in physical mode, but their pointers must not be converted until
`SetVirtualAddressMap` is called with the EFI memory map and the HHDM offset subtracted.
[UEFI-7.4.6] [UEFI-8.4.1] [LIMINE-EFIMM]

Stage 1 records the EFI system table and memory map in boot info and uses neither. This
keeps the physical/virtual transition out of the critical boot path. Supporting runtime
services is a separate decision (U6).

### D8. Syscall entry: trap gate first, `SYSCALL` fast path second

The first ring-3 test uses `int 0x80`. The fast path uses `IA32_STAR`, `IA32_LSTAR`,
`IA32_FMASK` and `swapgs` once the trap path, per-CPU state and user-copy rules are
proven. [SDM-2A] `SYSCALL` [SDM-4]

### D9. Object and handle model: opaque, generation-tagged, per-process

Handles are 64-bit opaque values resolved through a per-process table; the value encodes
an index and generation, never a kernel pointer. Rights are a bitmask. This satisfies
A2's need for stable handle/object concepts without freezing a Windows ABI.

### D10. DMA is constraint-driven, never device-class-driven

Generic memory exposes allocation against a caller-supplied `o0_dma_constraint` (address
mask, alignment, segment limit, coherency). Device-specific constraints come from the
bus/driver layer. No generic header names or assumes a GPU generation, an IOMMU topology
or a device class.

## 7. Unresolved choices

| ID | Question | Options | Recommended | Blocks |
|---|---|---|---|---|
| U1 | KPTI / Meltdown-class threat model | No KPTI; KPTI for user processes; per-CPU PCID-tagged | Decide before exposing untrusted user code; Stage 1 test thread is trusted | Security review, Stage 3 |
| U2 | Physical allocator design | Bitmap; buddy; hybrid | Bitmap for Stage 1, buddy only if fragmentation demands it | Stage 1 PMM |
| U3 | Preemption model | Voluntary; timer preemption; full priority scheduler | Timer preemption with round-robin, priority field reserved | Stage 2 |
| U4 | Driver location | In-kernel ring 0; userspace drivers | Ring 0 for Stage 1; userspace model deferred | A3, security |
| U5 | Syscall ABI freeze | Freeze now; defer to A2/A4 | Defer; keep numbers reserved | A2, A4 |
| U6 | EFI runtime services | Never; SetVirtualAddressMap at boot; on demand | Defer; record only | Power/reset paths |
| U7 | Legacy BIOS support | UEFI only; hybrid image | Hybrid image for test convenience, UEFI is the supported path | Boot matrix |
| U8 | 5-level paging default | 4-level; 5-level when present | 4-level default | Hardware support matrix |
| U9 | NUMA awareness | Ignore; SRAT-based | Ignore in Stage 1, validate SRAT only | Scheduler, later |
| U10 | Secure Boot | Unsigned Stage 1; signed release | Unsigned for QEMU; signing is a release requirement | Deployment |

## 8. Assumptions register

| ID | Assumption | Why it is safe now | Verification | If wrong |
|---|---|---|---|---|
| A1 | Target hardware is UEFI-class x86-64 with long mode, NX and SSE2 | All in-scope machines are 64-bit; Limine requires long mode | CPUID at boot; refuse to boot without long mode/NX | Boot stops with a clear message |
| A2 | Limine is the Stage 1 bootloader | D1 | Build and QEMU boot test | Boot contract must be re-expressed for the replacement loader |
| A3 | Firmware memory maps are sane and non-overlapping where Limine guarantees it | Limine normalises usable/reclaimable entries | Boot-time invariant checks | Mark overlapping regions reserved and continue degraded |
| A4 | QEMU TCG models enough architecture to test the contract | Sufficient for paging, IDT, APIC, SMP | Run the Stage 1 matrix under TCG and, where available, KVM | Move affected tests to hardware |
| A5 | ACPI tables are well-formed | Firmware contract | Checksum and length validation before use | Ignore the table, fall back to defaults |
| A6 | HHDM covers every region the kernel needs to touch | Limine maps usable, reclaimable, kernel, framebuffer, ACPI and reserved-mapped | Boot-time coverage check | Map the region explicitly in kernel page tables |
| A7 | All DMA is trusted because no IOMMU is active | Limine disables VT-d and AMD-Vi by default [LIMINE-STATE] | Confirm on target hardware | Untrusted drivers need IOMMU enablement before use |
| A8 | Secure Boot is disabled in Stage 1 | QEMU/OVMF test setup | Documented per test | Signed image and key management required |
| A9 | TSC is invariant only if CPUID says so | CPUID.80000007H:EDX[8] | Detect and select clocksource | Use APIC timer or HPET as clocksource |
| A10 | One NUMA node in Stage 1 | Test machines and QEMU default | SRAT sanity check | Memory policy becomes wrong, not unsafe |

## 9. Non-goals

- No final Windows ABI, no NT object semantics, no registry.
- No filesystem, network stack, USB stack, audio or graphics driver.
- No AML interpreter, S3/S4 power management, or thermal policy.
- No architecture port, no 32-bit kernel, no legacy BIOS-first design.
- No microkernel claim. The driver model location is an open question (U4), not a
  decision made here.

## 10. Review checklist

- [ ] Every recommendation cites an Intel, UEFI, ACPI or Limine source.
- [ ] Boot alternatives are compared and the choice is marked pending.
- [ ] No device-class or GPU-generation assumption appears in generic interfaces.
- [ ] Every unknown and firmware assumption is listed.
- [ ] Interfaces are sketches with unstable signatures, not a frozen ABI.
