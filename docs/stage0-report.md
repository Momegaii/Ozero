# Stage 0 report — Ozero A1

Reporting fields follow the briefing format. This is a **draft for review**, not an
acceptance certificate. Nothing below claims runtime behaviour that was not executed.

## 1. Status

| Required output | Status | Where |
|---|---|---|
| Kernel architecture note | draft, review response applied | `docs/kernel-architecture.md` |
| Boot contract | draft, review response applied | `docs/boot-contract.md` |
| CPU baseline matrix | draft, review response applied | `docs/cpu-baseline.md` |
| Interface draft | draft (sketch) | `docs/interface-draft.md`, `include/ozero/` |
| Stage 1 plan | draft | `docs/stage1-plan.md` |
| Source index and pins | complete for this review | `docs/sources.md` |
| Stage 0 report | this document | `docs/stage0-report.md` |

Overall: **not accepted**. The director's second review and the action brief v2 asked A1
for nine actions; all are answered in this branch. No kernel implementation has started.
D1–D4 (global D-005, D-012 through D-014) and the review-specific decisions remain
pending director approval. IF-008 v1.1 is **accepted by A1** at A3 `b9a69d8`; AHCI
remains blocked on D-007, D-021 and A3's acceptance. IF-009 v1.0 is **accepted by A1**
at A2 `d02598a`; A3 v1.0 confirmation and A4 registry update remain pending.

## 2. Review response (23 November 2026)

| # | Review action | Status | Where |
|---|---|---|---|
| 1 | Pin exact Limine protocol commit, binary hash, header hash and base revision; refuse unsupported revision | Done | `docs/sources.md` "Pinned artefacts"; `docs/boot-contract.md` §2.2, §11 |
| 2 | Correct base-revision explanation, separating current revision from x86-specific changes | Done | `docs/boot-contract.md` §3; `docs/kernel-architecture.md` D1; `docs/sources.md` |
| 3 | Document AP lifecycle: parked stack, trampoline, MP response lifetime, AP switch, proof before reclaim | Done | `docs/boot-contract.md` §9.1–§9.4; `docs/stage1-plan.md` M7 |
| 4 | Define HHDM mapping predicates, checked arithmetic, MMIO distinction | Done | `docs/boot-contract.md` §5.1; `include/ozero/mm.h`; `docs/stage1-plan.md` M1 |
| 5 | Review A3 platform/DMA/block APIs: address width, IOMMU, coherency, lifetimes, reset, in-flight DMA | Done, with blocker | `docs/interface-draft.md` §11; `include/ozero/mm.h`, `include/ozero/driver.h` |
| 6 | Return a source-to-contract table for critical boot claims | Done | `docs/boot-contract.md` §14 (21 rows); `docs/cpu-baseline.md` §4.1 |

Definition-of-done check:

| DoD item | Status | Evidence |
|---|---|---|
| No ambiguous claim that revision 6 adds new x86 state | Met | `docs/boot-contract.md` §3, `docs/kernel-architecture.md` D1, `docs/sources.md` |
| Every entry-state register claim cites pinned source and Intel SDM section | Met | Per-row Source columns in §3.1–§3.4, §14 table, `docs/cpu-baseline.md` §4.1 |
| AP and bootloader-memory lifetime is explicit | Met | `docs/boot-contract.md` §8, §9.1–§9.4 |
| HHDM conversion checks mapped type, range and overflow | Met | `docs/boot-contract.md` §5.1; `o0_hhdm_to_virt` in `include/ozero/mm.h` |
| A1 signs A3 interface review or records blockers | Met, with one blocker | `docs/interface-draft.md` §11 |

## 2.1 Director review — 23 November 2026 (second round)

Stage 0 remains **not accepted**. A1 was asked for four actions. All four are answered;
none is an acceptance claim.

| # | Director action | A1 result | Where |
|---|---|---|---|
| 1 | Resolve `include/o0/` versus `include/ozero/` | Resolved: `include/ozero/` is the only public header root; `include/o0/` is removed; symbols keep the `o0_` prefix; the conformance script rejects the old path | D-009; `include/ozero/`; `tools/check-headers.sh` namespace check |
| 2 | Resolve ownership of `tools/check-headers.sh` | Resolved: A1 owns the script and `tests/abi_static_asserts.c`; A4 owns CI integration and must invoke the script unmodified; A3 owns other `tools/` content; A4 adds file-level `CODEOWNERS` overrides | D-010; `AGENTS.md`; `docs/decisions/a1-global-decisions.md` |
| 3 | Review A2's process, address-space, handle and syscall requirements | Reviewed with conditions. Requirements accepted as input; no ABI freeze; A2 owns Windows semantics and TEB/ASLR/handle policy; the kernel delivers neutral exception records | D-011, D-024 through D-030; `docs/a2-interface-review.md` |
| 4 | Confirm the exact DMA interface A3 must implement | A1 conditions issued. A3 implements `struct o0_device_ops`; no DMA domain, IOMMU or reset assumptions; final IF-008 sign-off waits for A3's published `platform.h` and `block.h` | D-007, D-020, D-021; `docs/a3-dma-contract.md`; `include/ozero/driver.h` |

Second-round definition-of-done:

| Item | Status | Evidence |
|---|---|---|
| One public namespace, enforced | Met and automated | `include/ozero/`; `tools/check-headers.sh` prints `PASS no include/o0` and `PASS no legacy o0/ includes` |
| Script ownership explicit | Met | D-010; `AGENTS.md`; script header |
| A2 requirements reviewed with dispositions | Met | `docs/a2-interface-review.md`, 14 groups and 7 open items |
| A3 DMA callbacks exact | Met on the A1 side | `struct o0_device_ops` in `include/ozero/driver.h`; `docs/a3-dma-contract.md` §4 |
| A3 headers reviewed | **Superseded by §2.2** | A3 published at `b9a69d8`; A1 accepted IF-008 v1.1 |
| No implementation started | Met | No `kernel/`, `arch/` or driver code in the branch |

## 2.2 Director action brief v2 — 23 November 2026

Stage 0 remains **not accepted**. A1 was asked to do five things in the action brief v2;
all are answered.

| # | Action | A1 result | Evidence |
|---|---|---|---|
| A1-1 | Final IF-008 v1.1 review at A3 `b9a69d8` | **ACCEPT.** A3's `platform.h` and `block.h` conform; A3's ten copies of A1 headers are byte-identical to A1 `30ee4f0`; A1's checks pass on A3's tree | `docs/if008-v1.1-review.md`; D-032 |
| A1-2 | Answer A1-REV-01…12 at A2 `50b7853` / evidence `807c447` | **Answered.** 4 Accept, 6 Change required, 2 Defer. A2 must update its stale `include/o0/`/`90b004f` references and rename its colliding local D-011/D-012/D-020 | `docs/a2-contract-review-response.md` |
| A1-3 | IF-009 review | **Superseded by §2.3:** IF-009 v1.0 is accepted at A2 `d02598a`; the earlier v0.1 review had no contract | `docs/if009-review.md`; D-033 |
| A1-4 | Preserve the DMA boundary | Contract-level validation of `o0_device_ops`, one contiguous buffer, `max_segment` splitting, completion timing, reset semantics and BAR UC/NX. No A3 driver code exists to inspect | `docs/if008-v1.1-review.md` §4 |
| A1-5 | Keep AHCI blocked | A1 sign-off is granted for IF-008 v1.1, but AHCI remains blocked on director D-007 and A3's acceptance. No runtime or DMA proof exists | `docs/if008-v1.1-review.md` §1, §7 |

Third-round definition-of-done:

| Item | Status | Evidence |
|---|---|---|
| IF-008 v1.1 explicit review decision published | Met: ACCEPT | `docs/if008-v1.1-review.md`; D-032 |
| A1-REV-01…12 answered against current A2 revision | Met: reconciled at A2 `d02598a`/`3f78477` | `docs/a2-contract-review-response.md` §1.1 |
| IF-009 reviewed | Met: ACCEPT v1.0 (A1 side) | `docs/if009-review.md`; D-033 |
| All decisions tied to exact immutable hashes | Met | A3 `b9a69d8`/`2dcc0fc`; A1 `30ee4f0`; A2 `d02598a`/`3f78477`; A4 `3ae7aca` |
| AHCI probe blocked | Met | `docs/if008-v1.1-review.md` §1; no driver code or runtime test |
| No implementation started | Met | No `kernel/`, `arch/` or driver code |

## 2.3 Director proceed brief — 23 November 2026

Stage 0 remains **not accepted**. The proceed brief asked A1 to review IF-009 v1.0
at A2 `d02598a` and reconcile the earlier A1-REV answers against the current A2
revision. All five actions are answered.

| # | Action | A1 result | Evidence |
|---|---|---|---|
| A1-1 | Review IF-009 v1.0 at A2 `d02598a`/`3f78477`; answer IF9-A1-1…3 | **ACCEPT (A1 side) with conditions.** IF9-A1-1 is change-required at the kernel-primitive level (no file-backed section at `30ee4f0`); IF9-A1-2 accepted with an A2 mapping-lifetime condition; IF9-A1-3 accepted. Contract blob `9be6c8b0…`; manifest blob `f505cb4d…`; five fixture hashes recorded | `docs/if009-review.md`; D-033 |
| A1-2 | Reconcile A1-REV-01…12 against A2 `d02598a`/`3f78477` | **Unchanged: 4 Accept, 6 Change required, 2 Defer.** The 12-question blob `f5d08487…` and requirements blob `490924d2…` are identical at `50b7853` and `d02598a`; A2's new commits are IF-009/provenance changes, not new kernel questions | `docs/a2-contract-review-response.md` |
| A1-3 | Preserve IF-008 ACCEPT, keep dependencies distinct | **Preserved at A3 `b9a69d8`.** A3's latest head `2dcc0fc` does not change the headers. A3 must accept §6; D-007 and D-021 remain pending; AHCI blocked | `docs/if008-v1.1-review.md` §7.1; D-032 |
| A1-4 | Keep A1 public headers stable | **Met.** No header change in this round; A1 `30ee4f0` remains authoritative; A3's ten copies are byte-identical | `git diff 30ee4f0..HEAD -- include tests tools` is empty |
| A1-5 | Refresh evidence provenance | **Met.** Exact A2/A3/A4/A1 hashes and logs recorded; compile-time checks are explicitly separated from runtime proof | this report; `docs/if009-review.md`; `docs/a2-contract-review-response.md` |

Proceed-brief definition of done:

| Item | Status | Evidence |
|---|---|---|
| IF9-A1-1…3 answered against IF-009 v1.0 at `d02598a` | Met | `docs/if009-review.md` §2 |
| A1-REV-01…12 reconciled against current A2 code/evidence | Met | `docs/a2-contract-review-response.md` §1.1 |
| Review records contain exact hashes and explicit decisions | Met | A2 `d02598a`/`3f78477`; A3 `2dcc0fc`; A4 `3ae7aca`; A1 `30ee4f0` |
| No claim of runtime Windows compatibility or hardware operation | Met | IF-009 L0; IF-008 interface-only; no driver code or boot test |

## 3. Evidence

- Repository: <https://github.com/Momegaii/Ozero> (shared source of truth)
- Branch: `a1/stage0-review` (integration through a pull request; `main` is not pushed to)
- Pull request: <https://github.com/Momegaii/Ozero/pull/1>
- Staging repository before integration: `/workspace/project`, branch `master`
- First-round deliverables commit: `4943538609c25dbcaf57d60016a61f1e58c56341`
  ("Stage 0 review response: pin boot stack, correct revision claim, tighten
  AP/HHDM/DMA contracts"). This report is a follow-up commit.
- Files in that commit:
  - `docs/boot-contract.md`, `docs/cpu-baseline.md`, `docs/interface-draft.md`,
    `docs/kernel-architecture.md`, `docs/sources.md`, `docs/stage1-plan.md`
  - `include/ozero/boot.h`, `include/ozero/driver.h`, `include/ozero/mm.h`, `include/ozero/status.h`
  - `tests/abi_static_asserts.c`
- Full repository file list: `git ls-files`.

### 3.1 Second-round commits

| Commit | Purpose | Scope |
|---|---|---|
| `03f8883` | `refactor: make include/ozero the single public header root (D-009)` | 10 headers moved, includes, tests, docs and the namespace check |
| `164360e` | `feat: expose driver DMA callbacks and A2 thread primitives` | `struct o0_device_ops`, `o0_device_register_ops`, thread user pointer and GS/FS base, ABI asserts |
| `c50e385` | `docs: answer the director's second Stage 0 review` | decisions D-009..D-030, A2 review, A3 DMA contract, interface and architecture updates |
| this report commit | records the second-round evidence and exact hashes | `docs/stage0-report.md` |

Third round (action brief v2):

| Commit | Purpose | Scope |
|---|---|---|
| `85df00573e338db903b030d5ddcbfb4dac009ee7` | `docs: accept IF-008 v1.1 at A3 b9a69d8 (D-032)` | IF-008 review, interface draft, decisions, AGENTS, README |
| `d9cdec1ea85159e140b67190111f2c18597fa459` | `docs: answer A2 A1-REV-01..12 and review IF-009 (D-033)` | A2 response, IF-009 review, action brief v2 PDF |
| this report commit | records the action brief v2 evidence and exact hashes | `docs/stage0-report.md` |
| `a17d126664ba9c54792612590268f97d7f439b7d` | `docs: review IF-009 v1.0 at A2 d02598a (D-033)` | IF-009 v1.0 review, A2 reconciliation, IF-008 dependency status, proceed PDF |
| this commit | records the A1 review hash and final provenance | `docs/stage0-report.md` |

Reviewed inputs, not A1 commits:

- A2 branch `agent/a2-stage0` at `f324b28d943c34adc27a307f44c4e4b248fe3410` (second
  review), `50b78534f6b52af414f4ae5bc6e190c4427beba9` / evidence
  `807c447a175eba27624f9c50c9b2e676e95e1f20` (third review), and
  `d02598afe42ea0036a130a69801b3041f4b4cf9f` / evidence
  `3f7847756f256761a4da0ed393d2762111af22db` (proceed brief; IF-009 v1.0).
- A3 branch `a3/stage0-review-response` at
  `b9a69d855360df28ad7503f3ba748dcde0910471`; branch head
  `15cd9ce990c98af385d056016312dc27f5fc0856` is documentation-only; latest
  `2dcc0fcc6371aa6167ca773321e76ca9d5a587c0` responds to the proceed brief.
- A4 branch `a4/stage0-review-followup` at
  `245007d2b96bbc12de1e831c5275587ef56ee81f`; latest
  `3ae7acaf4d81d103063a33ca49c1739bdee316f8` is its round-3 audit.
- A3 `platform.h` and `block.h` were not published at the second review; they were
  published and reviewed at the third review.

### 3.2 Commands and raw output

Header and ABI checks:

```
$ ./tools/check-headers.sh
== standalone header compilation ==
PASS  boot.h
PASS  cpu.h
PASS  driver.h
PASS  ipc.h
PASS  irq.h
PASS  mm.h
PASS  o0.h
PASS  status.h
PASS  thread.h
PASS  types.h
== umbrella header, C and C++ ==
PASS  ozero/o0.h (C11)
PASS  ozero/o0.h (C++17)
== ABI static assertions ==
PASS  tests/abi_static_asserts.c
== generic-interface device neutrality ==
PASS  no device-class or GPU-generation terms in include/ozero
== freestanding headers only ==
PASS  no hosted libc headers in include/ozero
== public namespace ==
PASS  no include/o0
PASS  no legacy o0/ includes
RESULT: PASS
```

Limine pin verification (temporary directory, 2026-11-23):

```
$ sha256sum limine.h PROTOCOL_pinned.md limine-binary.tar.xz
bd2fa78d45353725952d7f9bd1fec17e3e19e4721af95f961d4d6b9285a1ffeb  limine.h
3e702cd9dbfa2ead76eab5b34479fbc507b031d599b56fb5eb1db743cfd08f68  PROTOCOL_pinned.md
493312624a0a7a3afadcf8e9993f5418a87258d6b54f9d7ebb914037fe089220  limine-binary.tar.xz

$ grep -n 'SUPPORTED_BASE_REVISION' limine-12.11.1/common/protos/limine.c
53:#define SUPPORTED_BASE_REVISION 6

$ diff -q limine.h limine-12.11.1/limine-protocol/include/limine.h
$ diff -q PROTOCOL_pinned.md limine-12.11.1/limine-protocol/PROTOCOL.md
(no output; both files identical to the pinned protocol revision)
```

A3 IF-008 v1.1 verification at `b9a69d8` (A1 conformance script run in A3's tree):

```
$ ./tools/check-headers.sh
PASS  block.h
PASS  platform.h
... (all ten A1 headers also PASS)
PASS  ozero/o0.h (C11)
PASS  ozero/o0.h (C++17)
PASS  tests/abi_static_asserts.c
PASS  no device-class or GPU-generation terms in include/ozero
PASS  no hosted libc headers in include/ozero
PASS  no include/o0
PASS  no legacy o0/ includes
RESULT: PASS
```

A3's own package checks:

```
$ make check
PASS: matrix validation
  rows: 10
  deferred: nvme-zns, sata-port-multiplier
PASS: deliverable check
  files: 24
  documents checked: 7
...................
Ran 19 tests in 0.005s
OK

$ make check-a1-headers A1_INCLUDE=/workspace/Ozero/include
cc -std=c11 -Wall -Wextra -Werror -I/workspace/Ozero/include -Iinclude -fsyntax-only tests/header_smoke.c tests/a1_header_smoke.c
exit 0
```

What this evidence shows:

- Headers are self-consistent, compile freestanding as C11 and C++17, documented ABI facts
  hold, generic interfaces contain no device-class or GPU-generation terms, no hosted
  libc leaks into public headers, and the only public root is `include/ozero/`.
- A3's `platform.h` and `block.h` compile standalone and against A1's actual headers, and
  A3's ten copies of A1's headers are byte-identical to A1 `30ee4f0`.
- The pinned Limine release (`v12.11.1`) supports base revision 6 and vendors exactly the
  pinned protocol revision.

What it does **not** show: any runtime behaviour. See §7.

## 4. Decisions needed

Global IDs are authoritative. Local IDs remain for cross-reference in the architecture note.
D-005 and D-007 already exist in A4's register; D-009 and D-010 are approved by A4 at
`245007d`; D-011 through D-033 are A1 proposals or interface records in
`docs/decisions/a1-global-decisions.md`. D-005, D-012, D-013 and D-014 unblock Stage 1.

| Global | Local | Decision | Options | Recommendation |
|---|---|---|---|---|
| D-005 | D1 | Boot path | UEFI + Limine base revision 6; custom UEFI loader; UEFI + another protocol | **Limine**, pending approval; comparison in boot contract §2 |
| D-012 | D2 | Paging mode | 4-level only; 5-level when available; 5-level default | **4-level default, 5-level opt-in** |
| D-013 | D3 | Kernel placement and direct map | Fixed kernel base + discovered HHDM; fixed HHDM; fully relocatable kernel | **Fixed `0xffffffff80000000` image, discovered HHDM, never hard-coded** |
| D-014 | D4 | Interrupt model | APIC + IOAPIC; legacy PIC; hybrid | **APIC/IOAPIC only**, PIC stays masked |
| D-015 | D5 | SMP startup | Limine MP feature; own INIT-SIPI-SIPI trampoline | **Limine MP feature**, kernel-controlled takeover |
| D-016 | D6 | ACPI depth | RSDP + MADT/FADT only; full AML interpreter | **MADT/FADT only** for Stage 1 |
| D-017 | D7 | EFI runtime services | Never; at boot; on demand | **Record only, do not use** in Stage 1 |
| D-018 | D8 | Syscall entry | `int 0x80` first, `SYSCALL` later; `SYSCALL` first | **Trap gate first**, fast path second |
| D-019 | D9 | Object/handle model | Opaque generation-tagged handles; pointer-like handles; NT-style handles | **Opaque 64-bit handles**, ABI unfrozen |
| D-020 | D10 | DMA interface | Constraint-driven; device-class-specific | **Constraint-driven**, no device assumptions |
| D-021 | D11 | IOMMU policy for Stage 1 | Trusted ring-0 drivers only; require IOMMU before any DMA | **Trusted ring-0 drivers only**, blocker recorded for untrusted drivers |
| D-022 | D12 | SDM citation policy | 2018 numbered sections + titles; require current-edition numbers before sign-off | **2018 numbered sections + titles**, since the current PDF is bot-blocked here |
| D-007 | D13 | A3 DMA/platform interface and limitations | Accept IOMMU blocker and no scatter-gather object; add both before A3 work | **Accept for Stage 1**, escalate before untrusted drivers |
| D-023 | D14 | Limine host tool | Build `limine` from the pinned source tarball; pin a distro package | **Build from the pinned source**, since the binary release has no Linux host tool |

Second-review decisions:

| Global | Decision | Recommendation |
|---|---|---|
| D-009 | Public header namespace | **Approved** (A4 `245007d`): `include/ozero/` only; `include/o0/` removed; symbols keep `o0_` |
| D-010 | Conformance tooling ownership | **Approved** (A4 `245007d`): A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI integration; A3 owns other `tools/` content |
| D-011 | A2 kernel-facing contract | **Accept A2 requirements as input**; A1 provides primitives; no ABI freeze; A1-REV-01…12 answered in `docs/a2-contract-review-response.md` |
| D-024 | Exception record shape | **Neutral kernel record**; A2 translates to Windows |
| D-025 | TEB ownership | **A2 owns layout/allocation**; A1 provides user pointer and GS/FS base |
| D-026 | Address-space layout/ASLR | **A2 owns policy**; A1 provides address-space primitives |
| D-027 | APC delivery points | **A1 defines return-to-user and alertable-wait points**; A2 owns wait semantics |
| D-028 | Handle inheritance | **A1 provides inherit flag and explicit list**; A2 chooses policy |
| D-029 | Clock resolution | **A1 provides monotonic nanoseconds**, measured not promised |
| D-030 | 32-bit and WOW64 | **Deferred**; outside A1 Stage 1 scope |

Third-review interface decisions:

| Global | Decision | Status |
|---|---|---|
| D-032 | IF-008 v1.1 **accepted by A1** at A3 `b9a69d8`; A3's `platform.h`/`block.h` conform; A1's ten headers are byte-identical. A3's latest head is `2dcc0fc`; AHCI remains blocked on D-007, D-021 and A3 acceptance. | Recorded by A1; A4 to register |
| D-033 | IF-009 v1.0 **accepted by A1** at A2 `d02598a`/evidence `3f78477`; contract blob `9be6c8b0…`; static-only L0. A3 v1.0 confirmation at `2dcc0fc` and A4 registry update at `3ae7aca` remain pending. | Recorded by A1; A3/A4 pending |

## 5. Unresolved risks

| Risk | Impact | Mitigation / status |
|---|---|---|
| No IOMMU in Stage 1 (Limine disables VT-d/AMD-Vi) | A buggy or malicious driver can DMA anywhere | Trusted ring-0 drivers only; recorded blocker D-021 |
| `o0_dma_buffer` describes one contiguous buffer | A3 cannot express a transfer larger than `max_segment` in one object | A3 splits the transfer into multiple buffers and requests; no SG pointer in `o0_io_request` |
| SDM numbers cited from the 2018 edition | Current-edition numbers differ for shifted chapters | Section titles are the stable anchor; D-022 |
| 5-level paging not covered by the 2018 mirror | LA57 claims are cited by chapter title only | Q5 test cell; D-012 |
| Parked APs and trampolines live in reclaimable memory | Early reclaim causes random AP triple faults | Hard proof gate (boot contract §9.3); no AP hotplug |
| Limine protocol pinned to a `trunk` commit | Upstream may move | Commit + SHA-256 pins; re-review on upgrade |
| No MBR harness exists | A4 must not conflate MBR boot with Limine boot | Labelled in Stage 1 plan §1 |
| QEMU success is not hardware support | Paging/APIC/AP bugs can hide in emulation | Hardware bring-up gate before any release claim |
| Secure Boot disabled in test | Production boot blocked | Signing is a release requirement, not a Stage 1 task |
| A3's IF-008 document is stale on D-009/XB-007 | A3 `2dcc0fc` still says A4 records `include/o0/`; A4 `245007d`/`3ae7aca` approves `include/ozero/` | A3 must correct the note; A4 must update the IF-008 registry row |
| A4's IF-008 registry row is stale | A4 `3ae7aca` still says A1 final approval pending; A3 published and A1 accepted v1.1 | A4 must update to 1.1, A1 accepted, D-007 pending |
| IF-009 v1.0 sign-off is incomplete | A1 accepted the A1 side; A3 v1.0 confirmation and A4 registry update are pending; XB-003 stays open | A3 confirms at `2dcc0fc`; A4 records A1/A3 signatures |
| A2's review references are stale | At `d02598a`, A2 still cites A1 `90b004f` / `include/o0/` and collides on D-009…D-023 | A2 updates to A1 `30ee4f0` / `include/ozero/` and renames its local IDs |
| A3 vendors A1's headers | Future drift could overwrite A1's headers on merge | Byte-identical at `b9a69d8`; A3 must not modify them; A1's headers win |
| No runtime proof of IF-008 | Compile checks do not prove DMA, IRQ routing, reset or BAR mapping | A1 sign-off is interface-only; AHCI stays blocked on D-007 and runtime tests |
| A2's contract conditions are unconfirmed | Exception, TEB, ASLR, APC and inheritance decisions could drift | D-024 through D-030; A2 must reply before any ABI freeze |
| `CODEOWNERS` overrides are not yet applied | A4's `/tools/` and `/tests/` rules would otherwise assign A1 artifacts to the wrong owner | A4 must add file-level overrides for `tools/check-headers.sh` and `tests/abi_static_asserts.c` (D-010) |
| A branch still using `include/o0/` will fail the build | Namespace drift after the rename | `tools/check-headers.sh` rejects the old root; A2/A3/A4 must include `<ozero/...>` |

## 6. Dependencies on other agents

| Agent | Dependency | State |
|---|---|---|
| Director | Approve D-005, D-007 and D-011 through D-033; D-009/D-010 are already approved | Open; Stage 0 is not accepted |
| A2 | Reply to `docs/a2-contract-review-response.md`; update stale `include/o0/`/`90b004f` references; rename local D-009…D-023; confirm D-024 through D-030. IF-009 v1.0 is published at `d02598a`; A3/A4 signatures remain. | Current pair `d02598a`/`3f78477`; no ABI freeze before A2 reply |
| A3 | A1 accepted IF-008 v1.1 at `b9a69d8`; A3 `2dcc0fc` must accept the §6 conditions, confirm IF-009 v1.0 at `d02598a`, correct the stale D-009/XB-007 note, and not modify A1's vendored headers. No AHCI probe before D-007/D-021 and A3 acceptance. | A1 sign-off granted; AHCI blocked |
| A4 | Add `CODEOWNERS` overrides for the A1 artifacts and A3's `platform.h`/`block.h`; update IF-008 to 1.1/A1 accepted; update IF-009 to 1.0/A1 accepted and add A1/A3; merge D-032/D-033; invoke the conformance script from CI unmodified; refresh the audit at `3ae7aca` | Open |
| A4 | Ratify build/toolchain; build the Limine host tool from the pinned source; verify repository history and evidence; distinguish the MBR harness from UEFI/Limine kernel boot | Open |
| A4 | Provide QEMU + OVMF CI so B1–B14 can actually run | Open; no boot test exists yet |

Cross-agent blockers carried in this report:

- A3 published IF-008 v1.1 at `b9a69d8`; A1 accepted it. AHCI remains blocked on D-007
  and A3's acceptance of the conditions. A3's IF-008 doc still says A4 records
  `include/o0/`; that is stale after A4 `245007d`/`3ae7aca`.
- IF-009 v1.0 exists at A2 `d02598a`; A1 accepted the A1 side. A3 v1.0 confirmation and
  A4 registry update remain pending; XB-003 is not closed.
- A2's review doc cites A1 `90b004f` and `include/o0/`, and uses D-009…D-023 with
  meanings that collide with the canonical register. A2 must update and rename.
- IOMMU absence blocks untrusted or userspace DMA. A1 recommends trusted ring-0 drivers
  for Stage 1; the director and A3 must accept or reject that (D-021).
- `o0_dma_buffer` cannot represent a transfer larger than `max_segment`; A3 must split the
  transfer into multiple buffers and requests. There is no SG pointer in `o0_io_request`.
- SDM citations use the 2018 numbered sections plus titles; the director must accept that
  policy or require current-edition numbers before sign-off (D-022).
- A2 must confirm the neutral exception record, TEB ownership, ASLR policy, APC delivery
  points, handle inheritance, clock resolution and 32-bit deferral (D-024 through D-030).

## 7. Not tested

Explicitly **not** done or not executed:

- No kernel implementation exists; no subsystem was written.
- No QEMU boot of any kind was run. The environment has no QEMU, cross toolchain,
  ISO tool or Limine host tool.
- No Limine ISO or disk image was built.
- No AP takeover, APIC routing, paging switch, exception self-test or reclaim was executed.
- No runtime test of `o0_hhdm_to_virt`; only its prototype and documented contract exist.
- No driver, AHCI probe or DMA transfer was executed.
- No 5-level paging test; the 2018 SDM mirror predates it.
- No IOMMU test; Stage 1 has no IOMMU.
- No physical hardware test.
- No A2, A3 or A4 sign-off has been received. A1 accepted IF-008 v1.1 at A3 `b9a69d8`;
  that is an interface sign-off, not A3 acceptance or runtime proof.
- No A3 driver code exists. No AHCI probe, DMA transfer, IRQ routing, reset or BAR mapping
  was executed.
- A2 published IF-009 v1.0 at `d02598a`; A1 accepted the fixture contract. A1 did not
  rebuild the fixtures and no fixture was executed; A1 copied the manifest hashes from
  A2's evidence.
- No A2 requirement was exercised. A2's evidence is static L0; this review accepts
  requirements, not runtime compatibility.
- The namespace check is compile-time only; no build or link of a kernel image exists.
- No Windows compatibility layer, syscall ABI or handle ABI was tested or frozen.
- No Secure Boot or signed-image test.

The executed checks are the compile-time header/ABI checks in §3.2, A3's package checks
at `b9a69d8`, and the Limine pin verification. None is a boot or DMA test.

Tool availability at report time (so the absence of a boot test is auditable):

```
qemu-system-x86_64     MISSING
clang                  MISSING
ld.lld                 MISSING
xorriso                MISSING
limine                 MISSING
gcc                    /usr/bin/gcc
```

Host `gcc` compiled the headers. No kernel image, ISO or boot test was produced.

## 8. Acceptance criteria check

| Criterion | Status | Evidence |
|---|---|---|
| Every architectural recommendation cites an Intel/UEFI/boot-protocol source | Met | Citation keys in every decision; `docs/sources.md` |
| Boot protocol alternatives compared; selection pending approval | Met | Boot contract §2, D1 marked pending |
| No Gen8+ Intel GPU assumptions in generic kernel memory interfaces | Met and automated | `tools/check-headers.sh` neutrality check |
| Boot contract implementable and testable in QEMU | Met as a plan | Ordered handoff, 14 assertions, Stage 1 QEMU matrix |
| All unknowns and CPU/firmware assumptions explicit | Met | Architecture note §7–§8; CPU matrix §6 |
| Source-to-contract traceability for critical boot claims | Met | Boot contract §14; CPU baseline §4.1 |
| One public namespace, enforced | Met and automated | `include/ozero/`; namespace check in `tools/check-headers.sh` |
| Conformance tool ownership explicit | Met; A4 override pending | D-010; `AGENTS.md` |
| A2 process/AS/handle/syscall requirements reviewed | Met with conditions | `docs/a2-interface-review.md`; `docs/a2-contract-review-response.md`; D-011, D-024 through D-030 |
| A3 DMA interface exact on the A1 side | Met: IF-008 v1.1 accepted at `b9a69d8` | `docs/if008-v1.1-review.md`; `struct o0_device_ops`; D-032 |
| IF-009 PE fixture contract reviewed | Met: ACCEPT v1.0 (A1 side); A3/A4 pending | `docs/if009-review.md`; D-033 |
| A1-REV-01…12 reconciled against current A2 | Met: `d02598a`/`3f78477`; 4/6/2 unchanged | `docs/a2-contract-review-response.md` §1.1 |
| Every decision tied to an exact hash | Met | A3 `b9a69d8`/`2dcc0fc`; A1 `30ee4f0`; A2 `d02598a`/`3f78477`; A4 `3ae7aca` |
| AHCI probe blocked | Met | `docs/if008-v1.1-review.md` §1, §7; no driver code or runtime test |
| Global decision IDs used | Met | `docs/decisions/a1-global-decisions.md`; report §4 |
| No implementation started | Met | No `kernel/`, `arch/` or driver code |

## 9. Next step

Director review of D-005, D-007 and D-011 through D-033. D-009 and D-010 are already
approved. A1 accepted IF-008 v1.1 at A3 `b9a69d8` and IF-009 v1.0 at A2 `d02598a`. A3 `2dcc0fc`
must accept IF-008 §6 and confirm IF-009 v1.0, correct its stale D-009/XB-007 note, and
not modify A1's vendored headers. AHCI remains blocked on D-007/D-021 and A3 acceptance.
A2 must reply to `docs/a2-contract-review-response.md`, update its stale references and
ID collisions, and record A3/A4 signatures for IF-009. A4 must add the `CODEOWNERS`
overrides, update IF-008 to 1.1/A1 accepted, update IF-009 to 1.0/A1 accepted with A1/A3
reviewers, merge D-032/D-033, invoke `tools/check-headers.sh` from CI, and refresh its
audit. After those approvals, A4 ratifies the build layout and toolchain (including the
Limine host tool), then M0 (bootable image and serial banner) starts. Until then, no
kernel implementation.

Integration: the A1 branch `a1/stage0-review` is proposed in pull request #1
(<https://github.com/Momegaii/Ozero/pull/1>). Stage 0 is **not** accepted by this report;
it is evidence for director review.
