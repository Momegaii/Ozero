# ADR 0010: Memory hardening order — SMEP/SMAP now, KASLR/buddy/reclaim later

- **Status:** Accepted-provisional (SMEP/SMAP); the KASLR/buddy/reclaim deferral is superseded by ADR 0011
- **Date:** 2026-11-25
- **Wave:** 3
- **Supersedes:** none
- **Related:** ADR 0007 (user ABI), ADR 0009 (SMP), `docs/hardening-roadmap.md`

## 1. Context

Wave 2 already maps user pages with U/S bits, enforces W^X, enables NX and WP,
and puts a guard page below each kernel stack and the user stack. The revised
Wave 3 adds SMEP/SMAP, then KASLR, a buddy allocator and bootloader-memory
reclaim.

These are not one change. SMEP/SMAP are small architectural switches with a
clear test; KASLR changes link/load assumptions; a buddy allocator replaces the
bitmap PMM; reclaim depends on a boot-contract checklist. Bundling them would
make a failure unattributable.

## 2. Alternatives

| Alternative | Trade-off |
|---|---|
| **Land SMEP/SMAP first (chosen)** | Small, testable, and it closes a Wave 2 entry criterion. KASLR/buddy/reclaim stay separate and honest. |
| Enable all four in one change | Larger diff, unclear failure ownership, and reclaim can free memory still referenced by a bug. |
| Leave SMEP/SMAP off | Keeps the user-memory path permissive and leaves `STAC`/`CLAC` unexercised. |
| Software SMAP emulation | Not equivalent; it does not prove the hardware path. |

## 3. Decision

1. Enable CR4.SMEP and CR4.SMAP only when CPUID advertises them.
2. Keep two usercopy cores: one without `STAC`/`CLAC` for CPUs without SMAP and
   one with them. The C wrapper selects at runtime; the fault-fixup table
   recognises both.
3. Do not dereference user memory outside `o0_copy_from_user`,
   `o0_copy_to_user` and the argument checker. The syscall layer already obeys
   this rule.
4. Keep the existing guard pages: the null guard, the kernel-stack guard and
   the user-stack guard.
5. Do **not** claim KASLR, a buddy allocator or bootloader reclaim. The bitmap
   PMM and the `o0_pmm_release_bootloader_memory()` stub remain as they are
   until their own ADRs and tests exist.

## 4. Consequences

- `Haswell-v1` under QEMU reports SMEP on and SMAP off; that is a CPU-model
  fact, not a kernel failure.
- The SMAP path is verified locally with `-cpu max`; it is not a CI-matrix claim
  and not a physical-hardware claim.
- KASLR, buddy allocation and reclaim were deferred by this ADR and are now
  recorded in ADR 0011. The deferral itself is historical.
