# CPU baseline matrix (Stage 0)

Status: draft. Feature bits are defined in [SDM-2A] `CPUID`; MSR addresses in [SDM-4].
The baseline is the set of features the kernel requires to boot. Everything else is
optional and must be detected at runtime with a defined fallback.

Policy in one sentence: **no instruction or MSR is used unless a CPUID gate proved it
available, and every optional feature has a tested fallback path.**

## 1. Required baseline

The kernel refuses to boot (with a specific serial message) if a required feature is
missing. These are the minimum for a long-mode kernel with W^X, a local APIC and a
preemptible timer.

| Feature | CPUID bit | Why the kernel needs it | If absent |
|---|---|---|---|
| CPUID | leaf `0`, max basic leaf ≥ 1 | All detection depends on it | Refuse to boot |
| Long mode (LM) | `80000001H:EDX[29]` | 64-bit kernel | Refuse to boot |
| PAE | `01H:EDX[6]` | Architecturally required for long-mode paging [SDM-3A] "Paging" | Refuse to boot (implied by LM) |
| NX / execute-disable | `80000001H:EDX[20]` | W^X for kernel data and user pages | Refuse to boot (policy). Alternative is a documented no-execute-protection mode; not accepted for Stage 1 |
| SSE | `01H:EDX[25]` | x86-64 compiler ABI and kernel vector code | Refuse to boot |
| SSE2 | `01H:EDX[26]` | x86-64 baseline; doubles as the default FPU context | Refuse to boot |
| FXSAVE/FXRSTOR | `01H:EDX[24]` | Fast FPU/SSE context switch | Refuse to boot |
| MSR | `01H:EDX[5]` | `IA32_EFER`, `IA32_APIC_BASE`, syscall MSRs | Refuse to boot |
| Local APIC | `01H:EDX[9]` | Interrupt delivery; the design has no legacy-PIC path | Refuse to boot |
| CMOV | `01H:EDX[15]` | Compiler baseline for x86-64 | Refuse to boot |
| CX8 (`CMPXCHG8B`) | `01H:EDX[8]` | Atomic 64-bit primitives used by libc-style code | Refuse to boot |

Notes:

- NX being "required" is a policy decision, not an architectural consequence of long
  mode. It is recorded as such so the director can relax it if a target CPU lacks it.
- `CPUID.01H:EDX[28]` (HTT) is **not** used to decide the CPU count. Topology comes from
  ACPI MADT and the Limine MP list. HTT only says the package may support more than one
  logical processor.
- The TSC and CLFSH are treated as optional with a degraded fallback, not as boot
  blockers. TSC invariance is a separate, optional bit (below).

## 2. Optional features and fallbacks

Each row is detected per CPU, stored in `struct o0_cpu_features`, and used only through a
helper that checks the bit.

| Feature | CPUID bit | Use when present | Fallback when absent |
|---|---|---|---|
| x2APIC | `01H:ECX[21]` | MSR-based APIC access, 32-bit APIC IDs | xAPIC MMIO; 8-bit APIC IDs |
| TSC-deadline timer | `01H:ECX[24]` | One-shot per-CPU timer | APIC timer in one-shot or periodic mode |
| Invariant TSC | `80000007H:EDX[8]` | TSC as monotonic clocksource | APIC timer or HPET as clocksource |
| TSC | `01H:EDX[4]` | Timestamp counter, early delay calibration | APIC timer only; no TSC clocksource |
| CLFSH | `01H:EDX[19]` | Cache-line flush for DMA coherency | `WBINVD` (slow, system-wide) |
| RDTSCP | `80000001H:EDX[27]` | Cheap TSC + `IA32_TSC_AUX` read | `RDTSC` plus `IA32_TSC_AUX` MSR read, or per-CPU lookup |
| PCID | `01H:ECX[17]` | Tagged TLB entries, cheaper address-space switches | Full TLB flush on `CR3` write |
| INVPCID | `07H:EBX[10]` | Targeted TLB invalidation | `INVLPG` loops or full flush |
| SMEP | `07H:EBX[7]` | Prevent kernel execution of user pages | Page-permission-only protection (weaker) |
| SMAP | `07H:EBX[20]` | Prevent kernel data access to user pages outside copy windows | Careful pointer validation only (weaker) |
| UMIP | `07H:ECX[2]` | Block `SGDT`/`SIDT`/`SLDT`/`STR` in user mode | User can read descriptor table locations |
| FSGSBASE | `07H:EBX[0]` | Fast user TLS base updates | `WRMSR` to `IA32_FS_BASE`/`IA32_GS_BASE` on context switch |
| LA57 (5-level paging) | `07H:ECX[16]` | Opt-in 5-level address space | 4-level paging (default) |
| 1 GiB pages | `80000001H:EDX[26]` | Large HHDM mappings | 2 MiB and 4 KiB pages |
| XSAVE | `01H:ECX[26]` + `OSXSAVE` `[27]` | Extended context switch (AVX, AVX-512) | `FXSAVE`/`FXRSTOR` |
| AVX | `01H:ECX[28]` + `XGETBV` bit 2 | Vector code, if the kernel uses any | SSE2 only |
| AVX2 | `07H:EBX[5]` | Vector code | SSE2 only |
| AVX-512F | `07H:EBX[16]` + `XGETBV` bits 5–7 | Not planned for Stage 1 | SSE2 only |
| AES-NI | `01H:ECX[25]` | Accelerated crypto, later | Software implementation |
| RDRAND | `01H:ECX[30]` | Entropy source, later | Other entropy sources |
| RDSEED | `07H:EBX[18]` | Seed-quality entropy, later | Other entropy sources |
| PKU / OSPKE | `07H:ECX[3]` / `[4]` | Memory protection keys, later | No protection keys |
| CET-IBT | `07H:EDX[20]` | Indirect branch tracking, later | No CET |
| CET-SS | `07H:ECX[7]` | Shadow stacks, later | No CET |
| SYSCALL/SYSRET | `80000001H:EDX[11]` | Fast syscall path (D8) | `int 0x80` trap gate |
| SERIALIZE | `07H:EDX[14]` | Strong serialising barrier | `MFENCE` + `LFENCE` sequence |
| PAT | `01H:EDX[16]` | Write-combining framebuffer, cache attributes | MTRR-programmed regions; no WC without PAT |
| PGE | `01H:EDX[13]` | Global TLB entries for kernel mappings | No global bit; flush kernel mappings on `CR3` write |
| MTRR | `01H:EDX[12]` | Read-only use of firmware MTRR state | Assume firmware left a coherent map; do not reprogram |
| VMX / SVM | `01H:ECX[5]` / `80000001H:ECX[2]` | Not used in Stage 1 | n/a |
| Hypervisor present | `01H:ECX[31]` | Recorded for diagnostics and test expectations | Assume bare metal |

Not used, deliberately: `PCLMULQDQ`, `MOVBE`, `BMI1/2`, `ADX`, `SHA`, `GFNI`, `VAES`,
`VPCLMULQDQ`, `AMX`, `MPX`, `TSX`, `SGX`, `PT`, `PKU` enforcement, `CET` enforcement,
`5-level paging as default`. Listing them here prevents accidental reliance.

## 3. Detection procedure

Order matters: extended leaves are unavailable until `CPUID.80000000H` says so.

1. `CPUID.0` → maximum basic leaf and vendor string. Require maximum ≥ 1.
2. `CPUID.1` → family/model/stepping, basic feature bits, CLFLUSH line size
   (`EBX[15:8]`), initial APIC ID (`EBX[31:24]`).
3. If maximum basic leaf ≥ 7: `CPUID.7.0` → structured extended features.
4. `CPUID.80000000H` → maximum extended leaf. Require ≥ `80000001H`.
5. `CPUID.80000001H` → long mode, NX, SYSCALL, 1 GiB pages, RDTSCP.
6. `CPUID.80000007H` (if available) → invariant TSC.
7. `CPUID.80000008H` (if available) → physical and linear address bits. Use these to size
   physical address masks; never assume 46/48 bits.
8. If `OSXSAVE` is set, `XGETBV(0)` → supported XCR0 mask. Enable only the intersection of
   CPU-supported and kernel-supported bits, in this order: x87, SSE, AVX, AVX-512.

MSR policy:

- Write only architectural MSRs listed in [SDM-4]. Do not probe for MSR presence by
  catching `#GP` in production code; gate on CPUID instead.
- `IA32_EFER` (NXE, SCE), `IA32_APIC_BASE`, `IA32_PAT`, `IA32_GS_BASE`,
  `IA32_KERNEL_GS_BASE`, `IA32_STAR`, `IA32_LSTAR`, `IA32_FMASK`, `IA32_TSC_AUX` are the
  Stage 1 set.
- x2APIC MSRs occupy `0x800`–`0x8FF`; access them only when x2APIC is enabled.

Per-CPU consistency:

- Detect features on the BSP and on every AP. If an AP lacks a feature the BSP enabled,
  park that AP and report it. Do not run a system with a silently heterogeneous feature
  set.
- `XCR0` must be identical on all CPUs before any AVX state is saved.

## 4. Control-register and compiler policy

| Item | Policy | Reason |
|---|---|---|
| `CR0.MP`, `CR0.NE` | Set to 1 before any x87/SSE instruction | Entry state has `MP=0`, `NE=0` [LIMINE-STATE] |
| `CR0.EM`, `CR0.TS` | Clear | Avoid `#NM`/emulation faults |
| `CR4.OSFXSR`, `CR4.OSXMMEXCPT` | Set | Enable SSE state and unmasked SIMD exceptions |
| `CR4.OSXSAVE`, `XCR0` | Set only when XSAVE is used | AVX/AVX-512 state must be enabled before use |
| `CR4.SMEP`, `CR4.SMAP` | Set when available | Defence in depth for user/kernel separation |
| `CR4.PGE` | Set when available, after page tables are stable | Global kernel mappings |
| `CR4.PCIDE` | Set only with a tested PCID allocator | A stale PCID is a security bug |
| `CR0.WP` | Already set at entry; never clear | Kernel cannot write read-only pages |
| Entry translation unit | `-mgeneral-regs-only` (or hand-written asm) | Entry state does not guarantee SSE |
| Whole kernel | `-mno-red-zone`, `-mcmodel=kernel`, 16-byte stack alignment | Interrupts use the current stack; the red zone would be clobbered |
| Kernel image | Non-PIE at `0xffffffff80000000` | Limine requirement [LIMINE-LAYOUT] |

### 4.1 Source traceability for entry-state register claims

Numbers are from the 2018 combined Vol. 3A edition (see `docs/sources.md`); the current
edition inserts a separate "Paging" chapter and shifts later chapter numbers, so the
section title is the stable anchor.

| Claim | Intel SDM section | Limine source |
|---|---|---|
| `CR0.PE`, `PG`, `WP`, `ET`; `MP`, `NE`, `EM`, `TS` clear | Vol. 3A §2.5 "Control Registers" | [LIMINE-STATE] |
| `CR4.PAE`; `LA57` only if requested; `OSFXSR`, `OSXMMEXCPT`, `OSXSAVE`, `SMEP`, `SMAP`, `PGE`, `PCIDE` clear | Vol. 3A §2.5; 5-level only in the current "Paging" chapter | [LIMINE-STATE] [LIMINE-PAGING] |
| `EFER.LME`, `LMA`, `NXE`; `SCE` clear | Vol. 3A §9.8.5 "Initializing IA-32e Mode", §4.13 "Page-Level Protection and Execute-Disable Bit"; Vol. 4 `IA32_EFER` | [LIMINE-STATE] |
| `RFLAGS.IF`, `DF`, `VM` clear; `RFLAGS=0x2` | Vol. 3A §2.3 "System Flags and Fields in the EFLAGS Register" | [LIMINE-STATE] |
| `CS=0x28`, data selectors `0x30`, bases 0 | Vol. 3A §3.4.5 "Segment Descriptors", §3.5.1 "Segment Descriptor Tables" | [LIMINE-STATE] |
| `IDTR` base 0 limit 0; IST not available | Vol. 3A §2.4.3 "IDTR", §5.10 "IDT", §5.14.5 "Interrupt Stack Table" | [LIMINE-STATE] |
| `TR` base 0 limit 0; no TSS | Vol. 3A §2.4.4 "Task Register (TR)", §6.2.1 "Task-State Segment (TSS)" | [LIMINE-STATE] |
| Local APIC enabled; `SVR=0x1FF`, `TPR=0`, LVTs masked | Vol. 3A §8.4.3 "Enabling or Disabling the Local APIC", §8.5.1 "Local Vector Table", §8.8.6 "Task Priority in IA-32e Mode", §8.9 "Spurious Interrupt" | [LIMINE-STATE] |
| `PAT0=WB`, `PAT5=WC`; MTRRs firmware-set | Vol. 3A §10.12 "Page Attribute Table (PAT)", §10.11 "Memory Type Range Registers (MTRRs)" | [LIMINE-CACHE] |
| SSE enable sequence | Vol. 3A §9.6 "Initializing SSE/SSE2/SSE3/SSSE3 Extensions" | [LIMINE-STATE] |

## 5. Test envelope

QEMU is the first test environment, not proof of hardware support.

| CPU model | Purpose | Notes |
|---|---|---|
| `qemu64` | Minimum baseline | Few optional features; exercises most fallbacks |
| `max` | Feature-rich TCG | Exercises x2APIC, SMEP/SMAP, PCID, maybe LA57 |
| `host` (KVM) | Real hardware feature set | Only where KVM is available; not a substitute for physical testing |
| `-smp 1` / `2` / `4` | UP and SMP paths | AP takeover and feature-consistency checks |

Acceptance for the matrix: for each model, the kernel must either boot with the expected
feature report or refuse with the documented message. A missing optional feature must
never cause an illegal-instruction fault.

## 6. Open questions

- Is NX truly mandatory for the product, or is a no-W^X degraded mode acceptable on a
  legacy target? (Affects the refuse-to-boot policy.)
- Is 5-level paging a product requirement, or only a test mode? (D2/U8)
- Which minimum QEMU CPU model defines CI? `qemu64` is convenient but may hide bugs in
  paths that only appear with more features.
- Are AVX/AVX-512 ever used by kernel code, or only by user space? If user-only, the
  kernel still needs XSAVE-sized context areas and XCR0 management.
