# Stage 1 plan (draft)

Status: proposed. Stage 1 does not start until D1–D4 are approved and A4 has ratified the
build layout. Every milestone ends with a command, a raw serial log and an exit code; a
successful compile is not completion.

## 1. Preconditions

| Precondition | Owner | Why |
|---|---|---|
| D1 boot path approved (Limine base revision 6) | Director | The whole contract depends on it |
| D2 paging mode approved (4-level default) | Director | Page-table code and test matrix |
| D3 kernel placement and HHDM policy approved | Director | Linker script and boot info |
| D4 interrupt model approved (APIC, no PIC) | Director | IDT/APIC bring-up order |
| Build system and toolchain ratified | A4 | Stage 1 must not invent a competing build |
| QEMU + OVMF available in CI | A4 | All boot tests run there |
| Limine pinned to a known commit | A4 | Reproducible images |

Toolchain, if A4 has no preference: `clang --target=x86_64-elf` or `x86_64-elf-gcc`, with
`-ffreestanding -fno-stack-protector -mno-red-zone -mcmodel=kernel -mno-sse` for the entry
translation unit only. The kernel is linked at `0xffffffff80000000`; the linker script
keeps the Limine request markers and base revision tag in retained sections.

## 2. Milestones

### M0. Bootable image and serial banner

Work:

- Minimal entry stub in assembly, no C runtime.
- Limine configuration, ISO assembly, base revision tag, request markers retained by the
  linker script.
- COM1 initialisation by port I/O and a banner.
- `isa-debug-exit` for a machine-readable pass/fail code.

Test:

```
qemu-system-x86_64 -M q35 -m 2G -smp 1 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/o0.iso -boot d -display none -no-reboot \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -serial file:build/m0-serial.log
```

Expected serial (numbers vary; match the shape):

```
[o0] boot: limine base=6 loaded=6 fw=uefi64
[o0] stage1: m0 PASS
```

Exit code 33 (`isa-debug-exit` writes `(value << 1) | 1`; the kernel writes `0x10`).
Failure path writes `0x11` and exits 35.

Exit criteria: the banner appears under UEFI/OVMF and, if the hybrid image is built, under
legacy BIOS. A missing base-revision tag prints a refusal and exits non-zero.

### M1. Boot info capture and memory-map normalisation

Work:

- Static boot arena in BSS; copy every Limine response, the memory map, the CPU list, the
  EFI memory map and the RSDP pointer. [LIMINE-MEMMAP] [LIMINE-MP] [LIMINE-EFIMM]
- Normalise types and flags; sort check; overlap check for usable/reclaimable; HHDM
  coverage flags.
- Refuse-to-boot checks from boot contract §11.

Test: same QEMU command; assert B1–B3 and `reserved_violations=0` partial.

Expected:

```
[o0] boot: limine base=6 loaded=6 fw=uefi64
[o0] boot: hhdm=0x... kernel_phys=0x... kernel_virt=0xffffffff80000000
[o0] memmap: entries=27 usable=1900MiB reclaimable=12MiB
[o0] memmap: sorted=ok overlap=ok hhdm_coverage=ok
[o0] stage1: m1 PASS
```

Exit criteria: a hand-calculated total for a fixed QEMU memory size matches the kernel's
free/reclaimable totals. A deliberately corrupted map (test build) is rejected.

### M2. CPU baseline, FPU/SSE enable, per-CPU skeleton

Work:

- CPUID detection in the documented order; feature table; refusal on missing required
  features; per-CPU info for the BSP.
- `CR0.MP/NE`, `CR4.OSFXSR/OSXMMEXCPT`, `FNINIT`, `MXCSR`; then allow SSE C code.
- Optional: `CR4.OSXSAVE` and `XCR0` only if XSAVE is used.

Test: QEMU with `-cpu qemu64` and `-cpu max`; compare feature lines.

Expected:

```
[o0] cpu: nx=1 apic=1 x2apic=0 pcid=0 smep=0 smap=0 la57=0 phys_bits=40 virt_bits=48
[o0] cpu: required=ok optional=x2apic,pcid,smep,smap
[o0] stage1: m2 PASS
```

Exit criteria: no illegal-instruction fault on either model; refusal path verified with a
test build that hides NX.

### M3. GDT, TSS, IST and IDT with exception self-test

Work:

- GDT: null, kernel code, kernel data, user code, user data, TSS.
- TSS with IST1 (double fault), IST2 (NMI), IST3 (machine check) stacks.
- IDT trap gates; exception dump (vector, error code, RIP, CS, RFLAGS, CR2 for #PF).
- Self-test: `int3` handler returns; `ud2` handler advances RIP and returns; a deliberate
  #PF on a guard page is reported and recovered.

Test: QEMU with `-d int -D build/m3-qemu.log`.

Expected:

```
[o0] gdt: ok tss=0x... ist=ok
[o0] idt: ok vectors=256
[o0] selftest: int3=ok ud2=ok pf=ok
[o0] stage1: m3 PASS
```

Exit criteria: no triple fault; the QEMU interrupt log shows the expected vectors; a
deliberate double fault (test build) lands on IST1 and prints a distinct message.

### M4. Physical page allocator

Work:

- Bitmap allocator over `O0_MEM_FREE`; reserve kernel image, boot arena, firmware regions
  and all `O0_MEM_RECLAIMABLE` as pending.
- Order-based alloc/free; pattern test; double-free detection in debug builds.

Test: allocate/free self-test at boot plus a debug command over serial.

Expected:

```
[o0] pmm: free=1890MiB reserved_violations=0
[o0] pmm: selftest alloc=512 free=512 pattern=ok
[o0] stage1: m4 PASS
```

Exit criteria: no allocation ever lands in reserved, ACPI NVS, bad, kernel, framebuffer or
pending-reclaimable memory. The check is an assertion over every allocation in a debug
build, not a spot check.

### M5. Kernel page tables, own CR3, HHDM verification

Work:

- Build kernel page tables: W^X kernel image, HHDM for every HHDM-covered type, per-CPU and
  IST stacks, MMIO/framebuffer on demand.
- Load `CR3`; read back a known HHDM address; map a fresh page and verify write/read.
- TLB helpers and a guard-page test.

Test: QEMU with 2G and 8G memory (the 8G case checks above-4GiB regions).

Expected:

```
[o0] vmm: cr3=0x... kernel_wx=ok hhdm_readback=ok
[o0] vmm: map_unmap=ok guard_page=ok
[o0] stage1: m5 PASS
```

Exit criteria: no stale bootloader page tables in use; a write to a read-only kernel page
faults as expected; 8G configuration reports the correct HHDM coverage.

### M6. APIC, timer and interrupt routing

Work:

- Local APIC/x2APIC init; spurious vector; EOI; TPR.
- IOAPIC routing from MADT, including source overrides; unmask only after a handler exists.
- Timer: TSC-deadline when available, otherwise APIC timer; monotonic clock.
- IPI for TLB shootdown and reschedule.

Test: QEMU `-smp 1`; count timer ticks over serial for approximately one second.

Expected:

```
[o0] apic: mode=xapic id=0 timer=tsc_deadline
[o0] timer: ticks=10 hz=100 monotonic=ok
[o0] stage1: m6 PASS
```

Exit criteria: 10 ticks in 0.1 s within the test tolerance; no spurious interrupt storm;
masking a line stops delivery.

### M7. SMP takeover

Work:

- Request the MP feature; per-CPU areas and kernel stacks for APs.
- Publish each AP entry with a release store to `goto_address`; wait for acknowledgements
  with a timeout; verify feature-set consistency. [LIMINE-MP]
- Cross-check MADT CPU list against the MP list.

Test: `-smp 2` and `-smp 4`.

Expected:

```
[o0] smp: bsp=0 cpus=4 online=4
[o0] smp: apic_ids=0,1,2,3 features_match=ok
[o0] acpi: madt_cpus=4 ioapics=1 overrides=2
[o0] stage1: m7 PASS
```

Exit criteria: every AP acknowledges on its own kernel stack; no AP is left in bootloader
memory; a mismatched feature set parks the AP and reports it.

### M8. Reclaim, ring-3 entry and Stage 1 sign-off

Work:

- Run the reclaim checklist, release bootloader-reclaimable memory, allocate from the
  released range and verify patterns.
- Ring-3 test thread enters through `int 0x80`, writes a line through a kernel-mediated
  serial call, yields, and exits.
- Timer preemption between two kernel threads.
- `[o0] stage1: PASS` only when every assertion above is true.

Test: full matrix in §3.

Expected final lines:

```
[o0] reclaim: released=12MiB free=1902MiB verify=ok
[o0] user: ring3=ok syscall=ok
[o0] sched: preempt=ok
[o0] stage1: PASS
```

Exit criteria: all matrix cells pass or produce the documented refusal; logs are attached
to the milestone report.

## 3. QEMU matrix

| Cell | Command variation | Purpose |
|---|---|---|
| Q1 | `-cpu qemu64 -smp 1 -m 2G` | Minimum baseline, most fallbacks |
| Q2 | `-cpu max -smp 1 -m 2G` | Rich feature set |
| Q3 | `-cpu max -smp 4 -m 2G` | SMP takeover, IPIs |
| Q4 | `-cpu max -smp 1 -m 8G` | Memory above 4 GiB, HHDM coverage |
| Q5 | `-cpu max,la57=on -smp 1 -m 2G` | 5-level opt-in, if QEMU supports it |
| Q6 | no pflash, `-boot d` | Legacy BIOS hybrid image, if U7 is approved |
| Q7 | `-enable-kvm -cpu host -smp 4` | Hardware feature set, where available |

All cells use `-M q35 -display none -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04`
and `-serial file:build/<cell>-serial.log`.

## 4. Debugging

- `-d int,cpu_reset,guest_errors -D build/qemu.log` for exception and reset diagnosis.
- `-s -S` plus GDB `target remote :1234` for breakpoints and page-table inspection.
- QEMU monitor `info registers`, `info mem`, `info tlb` for paging state.
- A serial-only kernel log with a monotonic prefix, so a hang is localised to the last
  printed line.
- `-no-shutdown -no-reboot` keeps a triple fault in the emulator for inspection.

## 5. Risk gates

| Gate | Condition | Action |
|---|---|---|
| G1 | Base revision 6 not honoured by the pinned Limine on any cell | Stop; re-evaluate D1 before writing more code |
| G2 | AP takeover unreliable under TCG | Restrict Stage 1 to UP, escalate D5, do not ship a flaky SMP path |
| G3 | Reclaim causes any fault | Keep reclaim disabled, treat as a boot-contract bug, do not "fix" by leaking memory silently |
| G4 | 5-level paging not testable | Drop Q5 from Stage 1; keep 4-level as the only supported mode |
| G5 | Optional feature path causes an illegal instruction | Remove the feature use until the CPUID gate is proven; never paper over with a try/catch |

## 6. Evidence format

Each milestone report contains:

- Status: not started / in progress / draft / blocked / submitted for review.
- Commit hash and changed files.
- Exact command and raw serial log (committed under `evidence/stage1/<milestone>/`).
- QEMU exit code.
- Decisions needed, risks and one concrete next action.

No milestone is complete on the basis of a compile or a code review alone.
