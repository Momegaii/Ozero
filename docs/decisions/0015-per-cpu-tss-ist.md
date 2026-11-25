# ADR 0015: Per-CPU TSS, GDT images and IST stacks

- **Status:** Accepted (design note; no code yet; blocking dependency before Stage 6 uses
  SMP beyond the BSP)
- **Date:** 2026-11-25
- **Wave:** design note alongside Wave 4.1; implementation before Stage 6 SMP use
- **Related:** ADR 0007 (GDT/TSS and the syscall entry), ADR 0009 (SMP bring-up),
  ADR 0011 (per-CPU scheduling), `include/ozero/gdt.h`, `kernel/arch/x86_64/gdt.c`,
  `kernel/src/smp.c`, `kernel/src/sched.c`

## 1. Context

Today the kernel has exactly one TSS and one GDT image:

- `kernel/arch/x86_64/gdt.c` declares one `static struct o0_tss tss`, one 8 KiB
  `double_fault_stack`, and one `gdt[O0_GDT_ENTRIES]` image.
- `o0_gdt_init()` builds that image, executes `ltr` once, and installs IST1 for the
  double-fault stack. It runs on the BSP only.
- `o0_gdt_load_for_ap()` loads the segment registers and programs the SYSCALL MSRs on an
  application processor. It never executes `ltr` and never gives that CPU an IST stack.
- `x86_64_idt_install_double_fault_ist()` sets `idt[8].ist = 1` in the shared IDT.
- `o0_arch_set_rsp0()` writes the single `tss.rsp0` plus the calling CPU's
  `o0_cpu_local.kernel_rsp0` mirror; `kernel/src/sched.c` calls it on every context
  switch.
- `kernel/src/smp.c` states the consequence plainly: user threads stay pinned to the
  BSP until per-CPU TSS/IST exists.

That combination is safe only while two conditions hold: no user thread ever runs on a
non-boot CPU, and no double fault ever occurs on a non-boot CPU. Both are accidents of
the current test set, not guarantees:

1. **Hardware interrupt from ring 3 uses `TSS.RSP0` of the CPU taking the interrupt.**
   With one TSS, an AP would load the BSP's current kernel stack top, so the AP's
   interrupt frame would land on another CPU's stack.
2. **The double-fault gate uses IST1 from the current CPU's TSS.** An AP has never
   executed `ltr`, so its task register is not a valid TSS selector. A `#DF` on an AP is
   therefore not delivered on a known-good stack; the optimistic outcome is a shutdown,
   the realistic one is a triple fault.
3. **NMI and machine check have no IST at all.** They currently use whatever stack is
   current, which is exactly what IST exists to avoid.
4. **A user thread cannot be moved to an AP** because items 1 and 2 make it unsafe, so
   Ring 3 and SMAP correctness on other CPUs is untested by construction.

The Director asked for this note before any Stage 6 work uses SMP. It is a design note:
no code is included, and nothing here changes the current BSP-pinned behaviour.

## 2. Alternatives

| Alternative | Trade-off |
|---|---|
| **Per-CPU TSS, per-CPU GDT image, per-CPU IST stacks (chosen)** | Each CPU owns its `rsp0` and IST pointers; selectors and GDT byte layout stay identical across CPUs, so `STAR`/`LSTAR`/`FMASK` and the uapi selectors do not change. Costs one 7-entry GDT image and one TSS per CPU plus IST stacks, and one `lgdt`/`ltr` per CPU at bring-up. |
| One global TSS (status quo) | Simplest, and correct while user threads and `#DF` stay on the BSP. Cannot express per-CPU `rsp0` or IST, so it blocks everything this note exists to unblock. |
| One GDT containing one TSS descriptor per CPU | One `lgdt` for the machine, but the descriptor table grows by two entries per CPU, the ABI-fixed `O0_GDT_ENTRIES` layout changes, and the host ABI test stops matching the documented selectors. Rejected: more ABI churn than the per-CPU image. |
| Per-CPU TSS with a shared GDT whose TSS descriptor is patched before each `ltr` | Races with every other CPU using the same descriptor and requires a GDT reload to change it; a TSS descriptor is cached in the task register, so patching it later is not equivalent to loading a different one. Rejected. |
| Entry only through `SYSCALL`, no TSS | `SYSCALL` already uses the per-CPU `GS:8` mirror, so it would work. But asynchronous interrupts from CPL3 have no other way to find a kernel stack, and IST is defined by the TSS. Rejected. |
| Per-CPU TSS only (no per-CPU GDT) with `ist` fields set at runtime | Works until a CPU needs a different TSS base: the descriptor in the one GDT points at one TSS. Rejected for the same reason as descriptor patching. |

## 3. Decision

1. **Per-CPU TSS.** `struct o0_tss o0_tss[O0_MAX_CPUS]`, 16-byte aligned, indexed by the
   CPU slot already used for `o0_cpu_local` (`info->extra_argument` on APs, 0 on the BSP).
2. **Per-CPU GDT image.** `uint64_t o0_gdt_images[O0_MAX_CPUS][O0_GDT_ENTRIES]`, each
   built by the existing `o0_gdt_build()` with that CPU's TSS base. Descriptor 5/6 (the
   64-bit TSS pair) differ per CPU; every other entry is byte-identical, so
   `O0_SEL_KERNEL_CODE`, `O0_SEL_KERNEL_DATA`, `O0_SEL_USER_*`, `O0_SEL_TSS` and the
   `STAR` value are unchanged. Each CPU loads its own image with `lgdt` and executes
   `ltr` with `O0_SEL_TSS` once, before it enables interrupts or schedules anything.
3. **IST stacks per CPU.** IST1: double fault, 8 KiB. IST2: NMI, 8 KiB. IST3: machine
   check, 4 KiB, reserved even though no `#MC` handler exists yet. The IDT stays shared
   (one table for the machine); the IST *index* is per-CPU safe because the stack pointer
   comes from the current CPU's TSS. Gates: vector 8 uses IST1 (already), vector 2 uses
   IST2, vector 18 uses IST3.
4. **`TSS.RSP0` is written only by the CPU that owns it.** `o0_arch_set_rsp0()` writes
   `o0_tss[current_cpu].rsp0` and the same CPU's `kernel_rsp0` mirror. The scheduler runs
   on the CPU it is switching on, so this needs no cross-CPU lock, but it does mean no
   code may call `o0_arch_set_rsp0()` "for" another CPU.
5. **`tss_ready` gate.** `struct o0_cpu_local` gains a `tss_ready` flag, set after the
   per-CPU `ltr` succeeds. The scheduler refuses to place a user thread on a CPU whose
   flag is clear, and the AP bring-up path does not enable interrupts before it. The flag
   is a scheduling constraint, not a diagnostic.
6. **Bring-up order on an AP:** paging (already) → per-CPU GDT image + `lgdt` → TSS
   `ltr` → IDT installation (already) → SYSCALL MSRs → LAPIC/timer → online marker. The
   existing order in `o0_ap_entry_c()` is preserved; only the GDT/TSS step becomes
   per-CPU.
7. **Interface change:** `o0_gdt_load_for_ap(void)` is replaced by
   `o0_gdt_init_ap(uint32_t cpu_index)`. Keeping the old name as a wrapper would hide the
   fact that a CPU without a `ltr` must not be scheduled; the call site must pass the
   index explicitly.
8. **No CPU hotplug.** GDT images and TSS blocks are built once. A CPU that goes offline
   never reuses another CPU's slot.

## 4. Consequences

- Memory cost is small and bounded: `O0_MAX_CPUS` (4) × (104-byte TSS + 56-byte GDT
  image) plus 20 KiB of IST stacks per CPU, all in kernel BSS.
- User threads may leave the BSP **only after** a `-smp 2` CI boot proves Ring 3, a
  contained user fault and the SMAP fixup on a non-boot CPU. Until that marker exists,
  `kernel/src/smp.c` keeps its BSP-pinning statement and it stays in `docs/handoff.md`.
- A `#DF` on an AP becomes diagnosable instead of a probable triple fault, and NMI/#MC
  stop borrowing an arbitrary stack.
- `abi_test` grows assertions over all `O0_MAX_CPUS` GDT images, so a future entry change
  that only updates CPU 0 fails the host suite.
- IST stacks in BSS have no guard page. Until the kernel stack region can supply them,
  an IST overflow corrupts adjacent static data; this is recorded as an open question
  rather than papered over.

## 5. Test plan

Host (`make test`, no QEMU):

1. For every CPU index, `o0_gdt_build()` produces identical code/data/user descriptors
   and a TSS descriptor whose base equals that CPU's TSS and whose limit is `0x67`.
2. IST pointers are 16-byte aligned, inside their own stack, and pairwise disjoint across
   CPUs and slots.
3. `offsetof(struct o0_tss, ist1) == 0x24` (already asserted) still holds, and the
   `kernel_rsp0` CPU-local offset (`8`) is unchanged so `syscall.S` needs no edit.

Emulator/CI:

4. Boot marker `OZERO: per-cpu tss OK cpu=N` after each CPU's `ltr`, printed with the
   loaded selector. `tools/qemu-smoke.sh` requires one line per online CPU under
   `-smp 2`.
5. A `-smp 2` ring-3 test with a user thread explicitly placed on CPU 1, doing a syscall
   and a contained faulting access; markers `user entry OK`, `user fault contained` and
   the SMAP fixup line must appear on that CPU. This is the evidence that unblocks
   unpinning.
6. Negative test: a placement request targeting a CPU with `tss_ready == 0` is refused
   with `O0_EBUSY`, not silently retried.
7. `#DF` injection stays behind a debug-only build flag. If it cannot be contained
   without killing the machine, it is reported as untested; it is never faked.

Physical: never part of the gate. Any physical run remains the Director's.

## 6. Open questions

1. **IST stack placement.** BSS (current proposal, no guard page) versus the kernel stack
   region once `o0_kstack_alloc()` can serve per-CPU fatal-fault stacks with an unmapped
   guard page. The proposal is BSS now, mapping later, with the limitation documented in
   both places.
2. **NMI policy.** A handler that only records and returns is safe; anything more needs a
   decision about what an NMI is allowed to interrupt.
3. **Machine check.** Whether a `#MC` handler is attempted at all in Stage 6, or the gate
   is installed and left to halt with a marker.
4. **Affinity interface.** Whether user threads get an explicit placement request in this
   wave, or placement stays an internal scheduler decision until Stage 6 defines it.
