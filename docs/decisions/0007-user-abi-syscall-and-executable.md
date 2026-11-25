# ADR 0007: user-mode ABI — syscall entry, register convention, error model and executable subset

- **Status:** Accepted — Director approval recorded 2026-11-24 in the Wave 2 execution
  directive, and implemented in the same change set. The implementation record in §7 lists
  every resolution and deviation.
- **Date:** 2026-11-24
- **Wave:** 2.1
- **Supersedes:** nothing. Relates to ADR 0002 (execution model), ADR 0003 (storage),
  ADR 0005 (LAPIC ExtINT interrupt delivery) and ADR 0006 (verification matrix).
- **Numbering note:** the Wave 2 directive referred to this decision as "ADR 0005". ADR 0005
  already exists in this tree (LAPIC ExtINT interrupt delivery), so the ABI decision takes the
  next free number, 0007. The SMP plan references ADR 0005 for interrupt delivery and this ADR
  for the ABI.
- **Blocks:** Wave 2.2 (per-process page tables), 2.3 (ring 3), 2.4 (user memory access),
  2.5 (ELF loader), 2.6 (negative tests), 2.7 (verification).

## 1. Context

The kernel today has no user mode. Facts from the current tree:

- No GDT, TSS or syscall code exists. The kernel runs on the GDT that Limine installs, whose
  selector values are the bootloader's choice, not ours
  (`grep -rn "lgdt\|syscall\|swapgs" kernel/` finds only CPU feature detection).
- `kernel/arch/x86_64/cpu.c` already detects CPUID.80000001h EDX bit 11 (`features.syscall`),
  so the fast-entry instruction can be required rather than probed at runtime.
- `kernel/arch/x86_64/paging.c` supports `O0_MAP_USER` and already marks intermediate tables
  user-accessible, but no caller sets the bit on a leaf mapping and no user address space
  exists. It keeps `EFER.NXE` off and enables `CR0.WP` (bit 16) during `o0_paging_load()`.
  Page-table pages come from a static 128-entry pool in BSS that is never recycled.
- Interrupt delivery now goes through the local APIC in virtual-wire ExtINT mode (ADR 0005,
  `kernel/arch/x86_64/pic.c`). The ring-3 work must keep that path intact: a TSS and an IST
  entry for the double fault are additions to the IDT, not a replacement of it.
- `include/ozero/status.h` states that `o0_status` is "deliberately not errno and not NTSTATUS"
  and that the user-visible translation is a later decision. This ADR is that decision.
- `include/ozero/thread.h` and `thread_internal.h` record that there is no user-mode state and
  no syscall ABI yet.
- SMP is deferred until Wave 2 is verified. Per-CPU data and `swapgs` must nevertheless be
  reserved now, so that enabling SMP later cannot force an ABI change.

The ABI decided here is the native Ozero ABI. It is not a Linux ABI with different numbers and
it is not a step toward one: Windows compatibility (Stage 7) is a separate layer with its own
entry contracts, and Linux/ELF conventions are used only where they are the natural x86-64
encoding (register argument order, ELF64 file format), never as a semantic blueprint.

## 2. Decision

### 2.1 Entry and exit mechanism

| Element | Decision |
|---|---|
| User → kernel | `syscall` (SYSCALL instruction, MSR `LSTAR`) |
| Kernel → user (return) | `iretq` from the syscall frame; `sysret` deferred as an optimisation |
| Kernel → user (first entry) | `iretq` with a constructed frame |
| User GS | `swapgs` on entry as the first instruction, `swapgs` as the last before return |
| Kernel GS base | `IA32_KERNEL_GS_BASE` (0xC0000102) holds `&o0_cpu_local[cpu]` |
| `EFER.SCE` | set once during CPU init |

`syscall` does **not** switch stacks: it loads RIP from `LSTAR`, clears RFLAGS bits from
`FMASK`, saves the return RIP in RCX and the caller RFLAGS in R11, and loads CS/SS from `STAR`.
The entry stub must therefore `swapgs`, load the kernel stack from the per-CPU block, and build
the frame itself.

`FMASK` (`SFMASK`, 0xC0000084) clears **IF, DF and TF** (`0x700`). IF and DF are required: the
kernel must not take an interrupt on a stack it has not yet established, and must not inherit a
user-set direction flag. TF is cleared so a user single-step cannot step into kernel code.

`iretq` is chosen over `sysret` for the return path. `sysret` faults (#GP) when RCX is not
canonical, which turns a corrupted return address into a kernel fault, and it derives SS/CS by
fixed arithmetic from `STAR`. `iretq` consumes the same frame that `swapgs` + `syscall` produced
and restores RFLAGS explicitly. `sysret` can be added later behind a canonicality check without
changing the ABI seen by user code.

### 2.2 GDT, TSS and STAR layout

| Selector | Descriptor | Notes |
|---|---|---|
| `0x00` | null | |
| `0x08` | kernel code, DPL 0, L=1 | `SYSCALL` target CS |
| `0x10` | kernel data, DPL 0 | `SYSCALL` target SS (`CS+8`) |
| `0x18` | user data, DPL 3 | `SYSRET` SS (`STAR[63:48]+8 \| 3`) |
| `0x20` | user code, DPL 3, L=1 | `SYSRET` CS (`STAR[63:48]+16 \| 3`) |
| `0x28` | TSS, 16-byte system descriptor | occupies `0x28`–`0x2F` |

- `STAR` (`0xC0000081`) = `(0x10 << 48) | (0x08 << 32)`.
  - `SYSCALL`: CS = `STAR[47:32]` = `0x08`, SS = `0x10`.
  - `SYSRET`: CS = `STAR[63:48] + 16 | 3` = `0x23`, SS = `STAR[63:48] + 8 | 3` = `0x1B`.
- `LSTAR` (`0xC0000082`) = `o0_syscall_entry` (assembly stub, kernel text).
- The TSS is one per CPU in the long run. For Wave 2 a single TSS is used, with:
  - `RSP0` (offset 0x04) updated on every context switch to the incoming thread's kernel stack
    top, 16-byte aligned;
  - `IST1` (offset 0x24) pointing at a dedicated 8 KiB double-fault stack, referenced by the
    vector-8 IDT gate. A double fault must not run on a stack that may already be corrupt.
- The GDT, TSS and IST stacks are kernel-owned and never mapped into a user address space.

### 2.3 Register convention

| Register | Role |
|---|---|
| `rax` | syscall number on entry; result or negative error code on return |
| `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` | arguments 1–6 |
| `rcx`, `r11` | clobbered by `syscall`; never used as arguments |
| all other registers | preserved across the call |

`r10` takes the fourth argument because `rcx` is destroyed by the instruction. The order
matches the x86-64 System V argument order with `rcx` replaced, so a compiler can emit a syscall
wrapper with six `mov`s and no shuffling, and so a future POSIX-shaped library maps onto it
without a translation layer. That is a calling-convention convenience, not a semantic
commitment to another system's syscalls.

### 2.4 Error model

`rax` carries either a non-negative result or a **negative native error code**:

- `0` means success.
- `-1 … -N` are Ozero-native codes defined in a frozen user header (`include/ozero/uapi.h`,
  to be added with the implementation). They are *not* errno values and their numbering is
  independent of POSIX.
- `rax < 0` is the single test for failure. Every result that can be returned directly is a
  small integer, a file-descriptor-like handle below 2^63, or a length, so the sign test cannot
  be confused with a legitimate value.
- The kernel maps `o0_status` to a user code in exactly one place, so the internal enum can grow
  without changing the ABI. A future POSIX or Win32 layer performs its own translation in user
  space; the kernel never emits another system's numbering.

`status.h` currently promises that the user-visible translation is a later decision. This is
that decision: the kernel keeps `o0_status`, the user boundary has its own frozen table.

### 2.5 Minimal syscall set (Wave 2)

| Number | Name | Arguments | Returns |
|---:|---|---|---|
| 0 | *(reserved)* | — | always `-EINVAL`; a zeroed register must not be a valid call |
| 1 | `exit` | `status` | does not return |
| 2 | `yield` | — | `0` |
| 3 | `debug_write` | `buf`, `len` | bytes written, or `-EFAULT` |
| 4 | `getpid` | — | process id |

Anything else returns `-ENOSYS`. The table is small on purpose: every entry must be verifiable
by a negative test before it is added. `debug_write` takes an explicit length; a NUL-terminated
interface would make the length a function of user memory the kernel has not validated yet.

### 2.6 User address-space layout

| Range | Use |
|---|---|
| `0x0000_0000_0000_0000` – `0x0000_0000_0000_FFFF` | null guard, always unmapped (64 KiB) |
| `0x0000_0000_0001_0000` – `0x0000_0000_3FFF_FFFF` | reserved for future heap/`mmap`; unmapped in Wave 2 |
| `0x0000_0000_4000_0000` – `0x0000_0000_7FFE_FFFF` | user image (`PT_LOAD` segments at their ELF vaddrs) |
| `0x0000_0000_7FFF_0000` – `0x0000_0000_7FFF_FFFF` | initial user stack, growing down |
| one page below the mapped stack | stack guard, always unmapped |
| `0x0000_8000_0000` – `0x0000_7FFF_FFFF_FFFF` | non-canonical hole, never usable |
| `0xFFFF_8000_0000_0000` – | HHDM (kernel only) |
| `0xFFFF_FFFF_8000_0000` – | kernel image (kernel only) |

Rules:

- The loader rejects any segment that leaves the user image window or touches the guard.
- User mappings never set the global bit, never carry the kernel's U/S=0 bit, and are never
  mapped in another process's tables.
- The kernel half stays mapped in every address space (no KPTI in Wave 2; recorded as an open
  risk, mitigated later by SMEP/SMAP and eventually by a separate kernel map).

### 2.7 Executable subset

The loader accepts one file shape only:

- `EI_CLASS = ELFCLASS64`, `EI_DATA = ELFDATA2LSB`, `EI_VERSION = 1`, `e_machine = EM_X86_64`,
  `e_type = ET_EXEC`.
- Program headers: `e_phentsize = 56`, `1 ≤ e_phnum ≤ 16`, `e_phoff` inside the file.
- `PT_LOAD` is honoured. `PT_GNU_STACK` and `PT_NOTE` are accepted and ignored.
  Everything else — `PT_INTERP`, `PT_DYNAMIC`, relocations, `PT_TLS`, `PT_PHDR` as a load
  segment — is rejected with a distinct error.
- Per segment: `p_filesz ≤ p_memsz`, `p_memsz > 0`, `p_vaddr` inside the user image window,
  `p_align` a power of two `≤ 2 MiB`, and `p_vaddr ≡ p_offset (mod p_align)` when `p_align > 1`.
- Segments must not overlap each other or the stack, and the total mapped size is bounded.
- The tail of a segment (`p_memsz > p_filesz`) is zero-filled.
- `W + X` in one segment is rejected (W^X), and the kernel maps user pages with NX unless the
  segment is executable.
- `e_entry` must fall inside an executable `PT_LOAD` segment.

Rejected inputs return a distinct error code so the negative tests can assert *which* rule
fired. The parser is written as a pure function over a byte buffer so host fuzz tests can drive
it without a process or a page table.

### 2.8 Per-CPU and `swapgs` reservation

`struct o0_cpu_local` is introduced now, even with one CPU:

```
struct o0_cpu_local {
    struct o0_cpu_local *self;      /* first field: swapgs gives a stable base */
    uint64_t kernel_rsp0;           /* TSS.RSP0 mirror */
    struct o0_thread *current;
    uint64_t user_rsp;              /* scratch across the entry stub */
    uint32_t cpu_index;
    uint32_t preempt_depth;
};
```

- `IA32_KERNEL_GS_BASE` points at `&o0_cpu_local[0]` from the first user entry onward.
- The array is declared as `[O0_MAX_CPUS]` with `O0_MAX_CPUS = 1` for Wave 2, so AP bring-up
  fills slots instead of changing the ABI, the entry stub or the MSR usage.
- `swapgs` is used unconditionally on entry and exit. There is no "is this already kernel"
  test, because user code can only enter through `syscall` and every kernel exit path restores
  the user GS base.

## 3. Alternatives considered

| Alternative | Why it was rejected |
|---|---|
| `int 0x80` gate as the primary entry | Needs a DPL=3 IDT gate, takes a full interrupt with stack switch through the TSS, and is several times slower than `syscall`. Kept as a possible future compatibility vector, not the native path. |
| `sysenter` / `sysexit` | 32-bit only; no 64-bit RIP or RSP handling. |
| Stack-passed arguments | Every argument would need user-memory validation before it could be read, and the kernel would have to copy a user frame; slower and harder to validate than registers. |
| `rcx` as the fourth argument | Destroyed by `syscall` itself. |
| Status in a second register (`rax` = value, `rdx` = status) | Every call site needs two instructions to test one condition; a single signed `rax` is cheaper and unambiguous for the value range in use. |
| POSIX errno numbering in the kernel | Couples the native ABI to another system's numbering and contradicts the stated intent of `status.h`; a compatibility layer can translate in user space instead. |
| `sysret` for the return path | Faults on a non-canonical RCX, turning a corrupted return address into a kernel fault. Deferred behind a canonicality check. |
| Separate syscall page (`vsyscall`-style) | Not needed for four syscalls; adds a mapping that must itself be trusted. |

## 4. Consequences

- **ABI freeze.** Numbers, argument order, error encoding and the `rax < 0` rule cannot change
  without a version bump. `include/ozero/uapi.h` becomes a frozen header.
- **Kernel stacks.** Every thread needs a kernel stack, and `TSS.RSP0` must be updated on every
  context switch. Wave 2.3 owns this.
- **User memory discipline.** Any syscall taking a pointer needs `o0_copy_from_user` /
  `o0_copy_to_user` with range checks and fault recovery (Wave 2.4). No syscall may dereference
  a user pointer directly.
- **Paging prerequisites.** NX, WP and W^X must be enforced before the first `iretq` to ring 3
  (Wave 2.2), and per-process page tables must exist so one process cannot see another's
  memory.
- **SMAP readiness.** Entry stubs must call through helpers so `STAC`/`CLAC` can be added
  without touching call sites (Wave 2.4).
- **SMP readiness.** Per-CPU data and `swapgs` exist from day one; the SMP plan references this
  ADR.
- **No Linux or NT semantics.** The native ABI is defined here. Stage 7 will add a PE/NT layer
  with its own entry contract; it must not reuse these numbers or assume these structures.

## 5. Verification plan

Host tests (no hardware required):

- GDT descriptor bytes: DPL, L bit, type, granularity, and the TSS descriptor's two slots.
- `STAR`/`FMASK`/`LSTAR` values, and the selector arithmetic for both `SYSCALL` and `SYSRET`.
- TSS offsets: `RSP0` at 0x04, `IST1` at 0x24, and 16-byte alignment of the double-fault stack.
- Error mapping: every `o0_status` maps to exactly one user code and the table is dense.
- User layout constants: null guard, image window, stack window, guard page.
- ELF parser: one acceptance case per segment shape and one rejection case per rule in 2.7,
  including truncated headers, overlapping segments, `W+X`, bad `p_align`, entry outside an
  executable segment, and oversized images.

Emulator evidence (QEMU BIOS and UEFI, `qemu64` and `Haswell-v1`), each reported separately.
CI runs QEMU 10.2.1 with OVMF 2025.11 (read from the run #133 logs); local runs in this sandbox
also cover QEMU 8.2.2 and 10.0.13 and are labelled local-only:

- `user entry OK` — ring 3 reached with the expected selectors.
- `syscall exit status=0` — `debug_write` then `exit` round-trip.
- `user fault contained` — a NULL dereference kills only the faulting process.

## 6. Open questions for the Director

1. **Error numbering.** Native table (recommended) or POSIX errno values at the boundary?
2. **`debug_write` shape.** Length-bounded (recommended) or NUL-terminated?
3. **Number space.** Reserve 0 as invalid and start at 1 (recommended), or allow 0 and grow from
   there? Also: reserve a range now for the future NT layer so numbers never collide.
4. **`yield`.** A real syscall (recommended for now) or a scheduler hint through a shared page?
5. **Stack placement.** `0x7FFF_0000` top with one guard page (recommended) or a randomised top
   from the start (KASLR is deferred, so a fixed top is honest for now)?
6. **Loader tolerance.** Accept and ignore `PT_GNU_STACK`/`PT_NOTE` (recommended) or reject
   anything that is not `PT_LOAD`?
7. **`sysret`.** Keep `iretq` only for Wave 2 (recommended) or add `sysret` with a canonicality
   check as part of 2.3?

## 7. Implementation record (2026-11-24)

Wave 2 landed this decision. The open questions in §6 were resolved as follows:

1. **Error numbering:** the native table is used. `o0_uapi_error_from_status()` maps every
   `o0_status` to `-1 … -18`; `0` means success. No errno values appear at the boundary.
2. **`debug_write`:** length-bounded, maximum 4096 bytes per call, copied in 128-byte chunks.
3. **Number space:** `0` is invalid; `exit`, `yield`, `debug_write`, `getpid` are `1 … 4`.
4. **`yield`:** a real syscall in Wave 2.
5. **Stack placement:** fixed top at `0x7FFF_0000`, one unmapped guard page below the four
   stack pages. KASLR remains deferred.
6. **Loader tolerance:** `PT_NOTE` and `PT_GNU_STACK` are ignored. Any other non-`PT_LOAD`
   program header is rejected with `O0_ENOTSUP`. An all-zero `PT_LOAD` (linker artifact, no
   bytes and no memory) is skipped; it maps nothing and is not a second kind of segment.
7. **Return path:** `iretq` only in Wave 2. `SYSRET` and its canonicality check are reserved.

Frozen values in the tree:

- `include/ozero/uapi.h`: syscall numbers, user error codes, user layout, and the user window
  helper shared by kernel and user build.
- `include/ozero/abi.h`: `O0_SEL_*`, `O0_ABI_STAR = 0x0010_0008_0000_0000`,
  `O0_ABI_FMASK = 0x700`.
- `include/ozero/cpu_local.h`: `swapgs` target, `kernel_rsp0`, `user_rsp`, `current`.
- `include/ozero/uaccess.h` + `kernel/src/usercopy.c` + `kernel/arch/x86_64/usercopy.S`:
  range and page-permission validation, then fixup-protected copy cores.
- `include/ozero/elf.h` + `kernel/src/elf.c`: the static ELF64 subset.
- `include/ozero/gdt.h` + `kernel/arch/x86_64/gdt.c`: GDT, TSS, `RSP0` and the IST1 double
  fault stack.
- `include/ozero/syscall.h` + `kernel/arch/x86_64/syscall.S` + `kernel/src/syscall.c`:
  entry stub, iretq frame, dispatcher and return value.
- `include/ozero/user.h` + `kernel/src/user.c`: image mapping, stack, process and user thread.

Deviations and defects found while implementing:

- **Syscall result register.** The first stub popped the saved user `RAX` after the C call,
  discarding the result. The result is now written into the saved slot before the pops. Caught
  by the `badptr` user program, which checks for `-O0_UE_FAULT`.
- **Address-space teardown.** The existing table walk stopped one level too high: it freed the
  page-table pages but not the user leaf frames, so the churn test found a PMM leak. It now
  walks `pml4 → pdpt → pd → pt`, frees user leaf frames and then the tables.
- **Context switch.** The scheduler now activates the incoming address space and sets
  `TSS.RSP0` to the incoming thread's kernel stack top before switching.
- **`swapgs`.** Interrupt and exception stubs test the interrupted `CS.RPL` and swap GS only
  for ring-3 transitions, on both entry and `iretq`.
- **No `STAC`/`CLAC` yet.** SMAP is deferred, so the cores do not emit them. The copy cores are
  the only place that needs to change when SMAP is enabled.
- **4 KiB leaves only.** The user loader maps only 4 KiB pages; a 2 MiB user leaf is not
  produced and would not be freed by teardown. The parser does not accept huge-page layouts.
- **Single CPU.** `o0_cpu_local` has `O0_MAX_CPUS = 1`; the field layout and `swapgs` are
  reserved, so SMP growth is an array-size and AP-init change, not an ABI change.

Host evidence: `tests/abi_test.c` and `tests/elf_test.c` in `make test` and
`make test-sanitize`. Emulator evidence: `tools/qemu-smoke.sh` requires the Wave 2 markers on
the BIOS/UEFI × `qemu64`/`Haswell-v1` matrix, locally on QEMU 10.0.13 and in GitHub Actions run
[#136](https://github.com/Momegaii/Ozero/actions/runs/45502990041) (commit `32c9e79`, QEMU
10.2.1, OVMF 2025.11). Physical evidence: none.

## 8. References

1. Intel, *Intel 64 and IA-32 Architectures Software Developer's Manual*, Vol. 2A
   (`SYSCALL`, `SYSRET`, `SWAPGS`), Vol. 3A (GDT, TSS, `STAR`/`LSTAR`/`FMASK`/`EFER`),
   Vol. 3B (interrupt and exception handling, double-fault IST).
2. Tool Interface Standard, *Executable and Linking Format (ELF) Specification*, version 1.2.
3. Ozero `docs/decisions/0001-boot-and-kernel-language.md`,
   `0002-stage3-thread-model.md`, `0003-stage4-storage.md`,
   `0005-lapic-extint-interrupt-delivery.md`, `0006-verification-matrix.md`.
4. Ozero `include/ozero/status.h` (status codes deliberately distinct from errno/NTSTATUS).
