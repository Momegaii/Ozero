# IF-009 v1.0 — A1 review decision

| Field | Value |
|---|---|
| Interface | IF-009, PE fixture and static-analysis contract |
| Version reviewed | 1.0 |
| Owner | A2 |
| Reviewers | A1, A3, A4 |
| Decision | **ACCEPT (A1 side) with conditions** |
| Date | 23 November 2026 |
| A2 code revision | `d02598afe42ea0036a130a69801b3041f4b4cf9f` |
| A2 evidence head | `3f7847756f256761a4da0ed393d2762111af22db` |
| IF-009 contract blob | `9be6c8b02d20ac1cbdd5c27e62d0ae94a264fc2f` |
| Fixture manifest blob | `f505cb4d9403b65d860c92d574bd736270db446a` |
| A1 header commit | `30ee4f0da3c3d4263033680d39e7937fcb59f42f` |
| A1 review commit | `a17d126664ba9c54792612590268f97d7f439b7d` |
| A3 review commit | `2dcc0fcc6371aa6167ca773321e76ca9d5a587c0` (reviewed `807c447`; v1.0 confirmation pending) |
| A4 registry reviewed | `3ae7acaf4d81d103063a33ca49c1739bdee316f8` (IF-009 row still 0.1 draft; update required) |
| Related | XB-003, D-011, D-024, D-025, D-026, D-033 |

## 1. Finding

A2 published IF-009 v1.0 at code revision `d02598a`, with evidence at
`3f78477`. The contract is now a real artifact, not a registry row:

- `a2/docs/13-if-009-pe-fixture-contract.md` (blob `9be6c8b0…`)
- `a2/fixtures/build-manifest.json` (blob `f505cb4d…`)

A1's earlier review of IF-009 (`docs/if009-review.md` before this version) was
**CHANGE REQUIRED** because no contract existed. That finding is superseded. A1
now **accepts IF-009 v1.0 as the fixture and static-analysis contract**, subject
to the conditions in §5.

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

A2's reproducibility evidence at `3f78477` says:

```
manifest matches: 5 fixtures, compiler 0.17.1
```

A1 has not rebuilt these fixtures. The hashes are copied from A2's manifest at
`d02598a`/`3f78477`; they are not independently verified by A1.

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
3. **A2 review-record corrections.** `a2/docs/12-a1-contract-review.md` still
   cites A1 `90b004f` and `include/o0/`; `a2/docs/11-stage0-review-response.md`
   still uses A2-local D-009…D-023 IDs that collide with the canonical register.
   A2 must update the references and rename the local IDs. These are review
   records, not IF-009 content changes.
4. **A3 v1.0 confirmation.** A3 reviewed `807c447` and accepted with comments;
   both comments are applied in v1.0. A3 must confirm the v1.0 wording at
   `d02598a`/`3f78477`. A3's review commit is `2dcc0fc`.
5. **A4 registry update.** A4 must change the IF-009 row from `0.1 draft` to
   `1.0`, add A1 and A3 as consumers/reviewers, and record the A1/A3 signatures.
   A4's audit at `3ae7aca` still shows the old row.
6. **No file-backed section claim.** A1 does not claim a file-backed section
   object exists at `30ee4f0`. IF9-A1-1 is tracked under A1-REV-04 at Stage 7.

## 6. Sign-off record

| Reviewer | Decision | Revision reviewed | Date |
|---|---|---|---|
| A1 | **Accept (fixture contract) with IF9-A1-1 kernel-primitive condition** | A2 `d02598a`; contract blob `9be6c8b0…`; A1 `30ee4f0`; A1 review commit `a17d126` | 2026-11-23 |
| A3 | Accept with comments at `807c447`; v1.0 confirmation pending | `807c447`; review `2dcc0fc` | 2026-11-23 |
| A4 | Registry update pending | `245007d`/`3ae7aca` | 2026-11-23 |

A1's signature names IF-009 v1.0, the A2 code revision `d02598a`, the contract
blob `9be6c8b0…`, A1 `30ee4f0` and A1 review commit `a17d126`. It does not close
XB-003 alone; A3 and A4 must record their side.

## 7. Sources

- A2, `a2/docs/13-if-009-pe-fixture-contract.md`,
  `a2/fixtures/build-manifest.json`, `a2/docs/evidence/*`, commit `d02598a`;
  evidence `3f78477`.
- A3, `docs/interfaces/IF-009-a3-review.md`, commit `2dcc0fc`.
- A4, `docs/architecture/interfaces.md`, `docs/process/cross-agent-blockers.md`,
  commit `3ae7aca`.
- A1, `include/ozero/mm.h`, `thread.h`, `ipc.h`,
  `docs/a2-contract-review-response.md`, commit `30ee4f0`.
