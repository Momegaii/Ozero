# IF-009 PE fixture contract — A1 review decision

| Field | Value |
|---|---|
| Interface | IF-009, PE fixture and static-analysis contract |
| Version in registry | 0.1 draft (registry row only; no interface document) |
| Owner | A2 |
| Reviewers | A1, A3, A4 |
| Decision | **CHANGE REQUIRED** |
| Date | 23 November 2026 |
| A2 code revision | `50b78534f6b52af414f4ae5bc6e190c4427beba9` |
| A2 evidence head | `807c447a175eba27624f9c50c9b2e676e95e1f20` |
| A1 header commit | `30ee4f0da3c3d4263033680d39e7937fcb59f42f` |
| A4 registry reviewed | `245007d2b96bbc12de1e831c5275587ef56ee81f` |
| Related | XB-003, D-011, D-024, D-025, D-026, D-033 |

## 1. Finding

A4's registry at `245007d` lists IF-009 as `0.1 draft`, owner A2, consumer A4,
status `Proposed`. A2's branch at `50b7853` / evidence head `807c447` has no
IF-009 interface document. It has a fixture corpus and a build manifest:

- `a2/fixtures/build-manifest.json`
- `a2/data/test-corpus.json`
- `a2/docs/evidence/pe-inspect.txt`
- `a2/docs/evidence/pe-structure-checks.txt`
- `a2/docs/evidence/reproducibility.txt`

That is evidence, not a contract. A1 cannot ACCEPT a versioned interface that
has not been published. A1 therefore records **CHANGE REQUIRED**: A2 must
publish IF-009 v0.1 with the contents in §4.

A1 does accept the fixture corpus and manifest as **input** and as static
evidence. A1 does not accept them as runtime proof, Windows compatibility proof,
or a frozen PE contract.

## 2. Ownership split A1 will sign

| Area | A1 | A2 | A3 | A4 |
|---|---|---|---|---|
| Process, thread, address space | Kernel primitives | PEB/TEB allocation, layout policy | — | — |
| Image mapping | `o0_as_map`/`o0_as_unmap` and later section objects | PE section mapping policy, relocations, imports | — | — |
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

## 3. Fixture inventory accepted as input

Manifest generated `2026-11-23T11:07:13Z`; compiler `python3 -m ziglang cc`
0.17.1; target `x86_64-windows-gnu`.

| Fixture | Output | Output SHA-256 | Size | Source SHA-256 | Evidence level |
|---|---|---|---|---|---|
| `console_hello` | `build/console_hello.exe` | `1e604cb5e651c27b4cc154225223217e98b3131e24602ed291bee2c7113e2f7c` | 73216 | `63ccdb77707949a62b78c3143fa127d783a0bc35a4d575649a587d19930cbec4` | L2 |
| `fileproc` | `build/fileproc.exe` | `2d6a6fbe6327ffc730c8a36368ee30a17560ad5c00a6f110815a9e1a4f71dbfd` | 74240 | `3e34988ee52a295314e1e99a7d8293c656d4cd329918db37b5f72a784033f41d` | L2 |
| `gui_messagebox` | `build/gui_messagebox.exe` | `61e763139e670ff842efe10cfc78298bb6e5f1f8bca3e5d96026a33ef6c38fc5` | 73216 | `65074a6321e83e100f9948f1e1dd467f955cdde39271d556af923575628e5545` | L3 |
| `tls_dll` | `build/tls_dll.dll` | `13bf410e9522732b73041f2ae3784509a28742a173c2469e997452a43055f9f6` | 70656 | `d560b6fb4b3f3d89c29912e56858f4ec79f5c9cf48460d6d0c5af4b452a1285e` | L2 |
| `cpp_throw` | `build/cpp_throw.exe` | `70403ee7002e94023f609e8d902f844f35f6bb7bcc47e1c8c9beca91eb653b9f` | 225280 | `10d50c5be2e7a97df8c1044c011522d853c1ffe31386e7c1b21612ca9e94600f` | L2 |

A2's reproducibility evidence at `807c447` says:

```
manifest matches: 5 fixtures, compiler 0.17.1
```

A1 has not rebuilt these fixtures. The hashes above are copied from A2's
manifest at `807c447`; they are not independently verified by A1.

## 4. Required contents of IF-009 v0.1

A2 must publish an interface document (path chosen with A4) containing:

1. **Identity** — interface ID, version, owner, status, date, consumers and
   reviewers.
2. **Scope** — Stage 0 is static-only. No fixture is executed. No Windows
   compatibility claim is made. Runtime execution is a later milestone with a
   separate evidence level.
3. **Fixture inventory** — the table in §3, with source and output SHA-256,
   compiler version, target triple, build command, size and expected evidence
   level.
4. **Ownership split** — the table in §2. A1 owns kernel primitives, not PE
   parsing; A2 owns PE/loader policy; A3 owns storage delivery; A4 owns the
   harness.
5. **A1 contract references** — the exact A1 header commit (`30ee4f0` or later)
   and the specific A1 primitives the fixtures exercise: address-space map,
   thread create, handle operations, exception delivery (when available).
6. **A2 contract references** — the exact A2 revision (`50b7853`) and evidence
   head (`807c447`), plus the PE structures and loader stages covered.
7. **Delivery path** — fixture bytes reach the loader through A3's block layer
   (IF-008), not through a kernel PE parser or a hard-coded in-kernel blob.
8. **Negative tests** — malformed PE, truncated headers, bad relocations, missing
   imports, invalid TLS, and the expected error/evidence for each.
9. **Hash drift policy** — rebuilding a fixture requires a new manifest and a
   review note; a hash change without a manifest change fails the harness.
10. **Open items** — 32-bit/WOW64 fixtures, managed/COR20 fixtures, graphics
    fixtures and any fixture that requires a Windows runtime remain explicitly
    deferred.

## 5. Required registry changes (A4)

A4's IF-009 row at `245007d` has only A4 as a consumer. A1 and A3 are affected:

- A1 provides the primitives the fixtures exercise.
- A3 provides the block/storage path for fixture bytes.

A4 must add A1 and A3 as consumers/reviewers, set the version to `0.1` when the
document is published, and keep the status `Proposed` until A1 and A3 record
their review. A4 must not mark IF-009 accepted on the basis of the manifest
alone.

## 6. A2-local ID collision

A2's review document uses D-011, D-012 and D-020 for A2-local questions. Those
IDs are taken in the canonical register (D-011 A2 contract, D-012 paging,
D-020 DMA model). A2 must rename them (`A2-Q3`, `A2-Q4`, `A2-Q12`) or use
canonical IDs before IF-009 is merged. This is the same collision noted in
`docs/a2-contract-review-response.md` §1.

## 7. Decision

**CHANGE REQUIRED.** A1 will review IF-009 v0.1 when A2 publishes it with the
contents in §4. A1 does not accept the current manifest as IF-009, and does not
claim the fixtures are runtime-compatible.

## 8. Sources

- A4, `docs/architecture/interfaces.md`, `docs/process/cross-agent-blockers.md`,
  `docs/decisions/decision-log.md`, commit `245007d`.
- A2, `a2/fixtures/build-manifest.json`, `a2/data/test-corpus.json`,
  `a2/docs/evidence/*`, `a2/docs/12-a1-contract-review.md`, commit `50b7853`;
  evidence `807c447`.
- A1, `include/ozero/mm.h`, `thread.h`, `ipc.h`, `docs/a2-contract-review-response.md`,
  commit `30ee4f0`.
- A3, IF-008 v1.1, `docs/if008-v1.1-review.md`.
