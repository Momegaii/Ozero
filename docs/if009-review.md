# IF-009 v1.0 — A1 review decision

| Field | Value |
|---|---|
| Interface | IF-009, PE fixture and static-analysis contract |
| Version reviewed | 1.0 |
| Owner | A2 |
| Reviewers | A1, A3, A4 |
| Decision | **ACCEPT (A1 side) with conditions** |
| Date | 23 November 2026 |
| A2 code revision | `6d305b80be14f9e217359fc0a9542aef9c9c8618` (previously `d02598a`) |
| A2 evidence head | `64b345840b32a3b992dfea23fe9b2a9e55da31e8` (previously `3f78477`) |
| IF-009 contract blob | `184b629064e3969c75e0e23883b4c9c4d111ee70` (previously `9be6c8b0`) |
| Fixture manifest blob | `f505cb4d9403b65d860c92d574bd736270db446a` |
| A1 header commit | `30ee4f0da3c3d4263033680d39e7937fcb59f42f` |
| A1 decision commit | `74b1471b217fb1c5e9bd72a75deef71a2d066c7b` |
| A1 substantive review commit | `a17d126664ba9c54792612590268f97d7f439b7d` |
| A1 R2 evidence commit | `9cda79c8ac2151e9c4e72caf9be5ab4d14d1f1ba` |
| A3 review commit | `307b919b41f6e65f1a40482588b84f92f715d67a` (v1.0 ACCEPT for hardware boundary; reviewed `d02598a`/`3f78477`) |
| A4 registry reviewed | `b4232a07ab88b6558b3020ae2463af16f0c325dd` (IF-009 row 1.0; A1/A3 accepted; A2 signature record pending) |
| Related | XB-003, XB-013, XB-016, D-011, D-024, D-025, D-026, D-033 |

## 1. Finding

A2 published IF-009 v1.0 at code revision `6d305b8`, with evidence at
`64b3458`. The contract is now a real artifact, not a registry row:

- `a2/docs/13-if-009-pe-fixture-contract.md` (blob `184b6290…`)
- `a2/fixtures/build-manifest.json` (blob `f505cb4d…`, unchanged)

A2's `6d305b8` adds the contract-reference and negative-coverage sections to the
v1.0 contract. The fixture inventory and manifest hashes are unchanged.

A1's earlier review of IF-009 (`docs/if009-review.md` before this version) was
**CHANGE REQUIRED** because no contract existed. That finding is superseded. A1
now **accepts IF-009 v1.0 as the fixture and static-analysis contract**, subject
to the conditions in §5. A3 accepted v1.0 for the hardware boundary at
`307b919`; A4 `b4232a0` records both interface decisions. A2's signature record
is still pending.

A1 does not accept a runtime claim. The contract itself says a pass is evidence
level **L0** and does not mean the image loads, imports resolve, TLS callbacks
run, exceptions dispatch or any Windows application is compatible. A1 agrees.

## 2. A1 answers — IF9-A1-1…3

| ID | Question | Decision | Exact consequence |
|---|---|---|---|
| IF9-A1-1 | Do `o0_as_create`, map and protect support a file-backed image at a chosen base, or is a section object required first? | **Change required at the kernel-primitive level; not an IF-009 version change** | `o0_as_create` + `o0_as_map` can map physical pages at a chosen `vaddr`; `o0_as_protect` sets per-range protections. There is no file-backed section object at `30ee4f0`. For M0, A2 can read the file bytes into A1-provided memory and map those pages. A true file-backed section (page cache, copy-on-write, demand paging) is A1-REV-04 at Stage 7. IF-009 explicitly does not freeze a kernel ABI, so this is a loader dependency, not an IF-009 defect. |
| IF9-A1-2 | Is the mapped image guaranteed to stay resident and readable, including `.pdata`, until the process exits? | **Accept with condition** | A1 does not evict or unmap a mapping behind the address-space owner. The mapping persists until `o0_as_unmap`, `o0_as_destroy` or process exit. Stage 1 has no demand paging or swap. A2 must keep `.pdata` mapped and must not unmap it while unwind data is needed. A1 does not guarantee against A2's own unmap or a driver bug. No `30ee4f0` header change. |
| IF9-A1-3 | Does A1 accept these five fixtures as M0 inputs without implying an ABI freeze or a Windows loader commitment? | **Accept** | A1 accepts the five fixtures as static M0 inputs. This is not an ABI freeze, not a Windows loader commitment, and not a runtime compatibility claim. A1 remains responsible for kernel primitives only; A2 owns PE parsing and loader policy. |

The IF9-A1-1 answer overlaps A1-REV-04 (section objects) and A1-REV-05
(address-space layout). The IF9-A1-2 answer overlaps A1-REV-09 (`.pdata`
lifetime). The dispositions in `docs/a2-contract-review-response.md` are
unchanged: 4 Accept, 6 Change required, 2 Defer.

## 2.1 Known gaps — visible, not covered

A2's v1.0 negative-coverage table is explicit about two gaps. A1 records them as
gaps, not covered tests:

| Gap | v1.0 state | A1 disposition |
|---|---|---|
| Truncated PE headers | Not covered by a negative case; candidate for v1.1 | A1 does not accept it as covered. A2 must add a truncation case before claiming complete header validation. |
| Missing DLL name in an import descriptor | Partial: `import_terminator` covers a missing terminator, not a missing DLL name; candidate for v1.1 | A1 does not accept it as covered. A2 must add the missing-DLL-name case in v1.1. |

A1 accepts v1.0 with these gaps explicitly deferred to v1.1. Neither gap may be
reported as a passing IF-009 test, and A2 must keep the gap table in the
contract.

## 3. Fixture inventory accepted as input

Manifest generated `2026-11-23T12:23:30Z`; compiler `python3 -m ziglang cc`
0.17.1; target `x86_64-windows-gnu`. Paths are relative to `a2/`.

| Fixture | Output | Output SHA-256 | Size | Source SHA-256 |
|---|---|---|---|---|
| `console_hello` | `build/console_hello.exe` | `1e604cb5e651c27b4cc154225223217e98b3131e24602ed291bee2c7113e2f7c` | 73216 | `63ccdb77707949a62b78c3143fa127d783a0bc35a4d575649a587d19930cbec4` |
| `fileproc` | `build/fileproc.exe` | `2d6a6fbe6327ffc730c8a36368ee30a17560ad5c00a6f110815a9e1a4f71dbfd` | 74240 | `3e34988ee52a295314e1e99a7d8293c656d4cd329918db37b5f72a784033f41d` |
| `gui_messagebox` | `build/gui_messagebox.exe` | `61e763139e670ff842efe10cfc78298bb6e5f1f8bca3e5d96026a33ef6c38fc5` | 73216 | `65074a6321e83e100f9948f1e1dd467f955cdde39271d556af923575628e5545` |
| `tls_dll` | `build/tls_dll.dll` | `13bf410e9522732b73041f2ae3784509a28742a173c2469e997452a43055f9f6` | 70656 | `d560b6fb4b3f3d89c29912e56858f4ec79f5c9cf48460d6d0c5af4b452a1285e` |
| `cpp_throw` | `build/cpp_throw.exe` | `70403ee7002e94023f609e8d902f844f35f6bb7bcc47e1c8c9beca91eb653b9f` | 225280 | `10d50c5be2e7a97df8c1044c011522d853c1ffe31386e7c1b21612ca9e94600f` |

A2's reproducibility evidence at `64b3458` says:

```
manifest matches: 5 fixtures, compiler 0.17.1
```

A1 has not rebuilt these fixtures. The hashes are copied from A2's manifest at
`6d305b8`/`64b3458`; they are not independently verified by A1.

## 4. Ownership split A1 signs

| Area | A1 | A2 | A3 | A4 |
|---|---|---|---|---|
| Process, thread, address space | Kernel primitives | PEB/TEB allocation, layout policy | — | — |
| Image mapping | `o0_as_map`/`o0_as_unmap`; section objects later | PE section mapping policy, relocations, imports | — | — |
| Handles and syscalls | Opaque handles, syscall entry/table, return path | NT handle table, `ntdll`-equivalent, NTSTATUS/Win32 mapping | — | — |
| Exceptions | Neutral `o0_exception_record`/`o0_context` delivery | Windows `EXCEPTION_RECORD`/`CONTEXT`, SEH dispatch | — | — |
| Unwind | Mapping lifetime only | `RtlVirtualUnwind`-equivalent, `.pdata` parsing | — | — |
| PE parsing | None | PE/COFF parse, imports, exports, TLS, load config, resources | — | — |
| Fixture bytes | None | Fixture sources and expectations | Block/storage delivery through IF-008 | Harness and CI |
| Fixture execution | None in Stage 0 | Static analysis only in Stage 0 | None in Stage 0 | Static checks only |

A1 must not embed PE knowledge in the kernel. A2 must not assume a kernel
syscall number, handle encoding, TEB/PEB layout or exception shape. A3 must not
parse PE; it supplies bytes through the block interface. A4 owns the harness and
the registry.

## 5. Conditions

1. **Static-only, L0.** A2 must keep IF-009 at evidence level L0 and must not
   claim load, import, TLS, exception, API-set or Windows compatibility results.
2. **Version discipline.** Any change to a fixture source, flag, target,
   compiler version or output hash bumps IF-009 to 1.1, 1.2, and so on. A1 and
   A3 re-sign; an old signature does not carry forward.
3. **A2 signature record.** A2 corrected the stale `90b004f`/`include/o0/`
   citations and the local D-009…D-023 IDs at `6d305b8`. A2 must now record
   A1's ACCEPT at `74b1471` (substantive review `a17d126`) and A3's ACCEPT at
   `307b919`, and update its ledger from `d02598a`/`3f78477` to
   `6d305b8`/`64b3458`.
4. **A3 v1.0 confirmation.** A3 accepted v1.0 for the hardware boundary at
   `307b919` (reviewed `d02598a`/`3f78477`). A2 must record that signature; no
   further A3 review is required unless the contract changes.
5. **A4 registry update.** A4 `b4232a0` records the IF-009 row at 1.0 with A1/A3
   accepted and A2's signature record pending. A4 must close XB-003 only after
   A2 records both signatures and the current evidence pair.
6. **Known gaps.** Truncated headers and the missing DLL-name import case are
   gaps/partial coverage, not covered tests. They are deferred to v1.1 and must
   remain visible.
7. **No file-backed section claim.** A1 does not claim a file-backed section
   object exists at `30ee4f0`. IF9-A1-1 is tracked under A1-REV-04 at Stage 7.

## 6. Sign-off record

| Reviewer | Decision | Revision reviewed | Date |
|---|---|---|---|
| A1 | **Accept (fixture contract) with IF9-A1-1 kernel-primitive condition; gaps deferred to v1.1** | A2 `6d305b8`; contract blob `184b6290…`; A1 `30ee4f0`; decision `74b1471`; substantive review `a17d126` | 2026-11-23 |
| A3 | **Accept v1.0 for the hardware boundary** | A2 `d02598a`/`3f78477`; A3 `307b919` | 2026-11-23 |
| A4 | Registry records A1/A3 acceptance; A2 signature record pending | `b4232a0` | 2026-11-23 |
| A2 | Signature record pending | `6d305b8`/`64b3458` | 2026-11-23 |

A1's signature names IF-009 v1.0, the A2 code revision `6d305b8`, the contract
blob `184b6290…`, A1 `30ee4f0` and A1 decision `74b1471`. It does not close
XB-003 alone; A2 must record A1's and A3's signatures and the current evidence
pair.

## 7. Sources

- A2, `a2/docs/13-if-009-pe-fixture-contract.md`,
  `a2/fixtures/build-manifest.json`, `a2/docs/14-review-ledger.md`,
  `a2/docs/evidence/*`, code `6d305b8`; evidence `64b3458`.
- A3, `a3/docs/interfaces/IF-009-a3-review.md`, commit `307b919`.
- A4, `docs/architecture/interfaces.md`, `docs/process/cross-agent-blockers.md`,
  commit `b4232a0`.
- A1, `include/ozero/mm.h`, `thread.h`, `ipc.h`,
  `docs/a2-contract-review-response.md`, decision `74b1471`.
- A1 raw log, `docs/evidence/a1-raw-checks-2026-11-23-r2.txt`.
