# IF-009 — A3 review of the PE fixture contract

| Field | Value |
|---|---|
| Interface | IF-009, PE fixture and static-analysis contract |
| Owner | A2 |
| A3 role | Hardware-boundary reviewer; not owner and not approver |
| Reviewed | A2 `agent/a2-stage0` @ `807c447`, `a2/docs/01-pe-loader-requirements.md`, `a2/fixtures/build-manifest.json` |
| A4 registry | IF-009 0.1 draft, owner A2, consumers A4 (A4 `245007d`) |
| A3 verdict | **No hardware-platform leakage found; ACCEPT WITH COMMENTS for A1/A2/A4** |
| Gate | XB-003 remains Open until A1/A3 review is recorded by A4 |

A3 does not own PE parsing, mapping or loader policy. This review checks only the
boundary: hardware and platform types from A1/A3 must not leak into the
Windows-facing loader contract or the PE fixture manifest.

## 1. Boundary rules A3 asks IF-009 to state explicitly

1. IF-009 fixtures are Windows PE binaries. Loader-facing requirements use
   Windows types (`IMAGE_*`, `HANDLE`, `NTSTATUS`) or opaque A2 types, not A3
   device types.
2. The IF-009 manifest and fixture sources must not contain `o0_*` hardware
   types, `ozero/...` headers, `o0_paddr_t`, `o0_dma_buffer`, `o0_device_t`,
   `o0_io_request`, `o0_block_dev`, `o0_bio`, AHCI, NVMe, USB or GPU types.
3. A1 memory and process primitives stay behind A2. A2 may require
   reserve/commit/protect/map behaviour, but the PE contract must not expose
   A1's internal page-table or DMA types.
4. A3 storage and device capabilities are not part of IF-009. If a fixture needs
   file I/O, it uses A2/A1 file objects; it does not call an A3 block driver.
5. Fixture manifests should use repository-relative paths and stable hashes.
   Absolute build paths are environment leakage, not a hardware-type leak, but
   they weaken reproducibility.

## 2. Findings against A2 `807c447`

| Check | Result | Evidence |
|---|---|---|
| Fixture sources contain no `o0_`/`ozero` hardware types | Pass | Searched `console_hello.c`, `cpp_throw.cpp`, `fileproc.c`, `gui_messagebox.c`, `tls_dll.c`; no matches |
| Fixture sources use Windows APIs, not A3 block/device APIs | Pass | `fileproc.c` uses `VirtualAlloc`/`VirtualFree`; no `o0_block_*` or `o0_device_*` |
| A2 loader requirements do not require A1 internal types | Pass | A2 names capabilities and Windows-visible semantics; A1 internals remain A1's design |
| Manifest is reproducible and path-neutral | **Comment** | `a2/fixtures/build-manifest.json` contains absolute `/workspace/project/Ozero/...` command paths. A2 should normalize to relative paths or document the build root. |
| IF-009 ownership and consumers are current | **Comment** | A4 `245007d` still lists IF-009 0.1 with consumers A4. A3 is a reviewer, not an owner; A4 should record A3 review in XB-003 and update the interface row when A2 revises it. |

## 3. A3 comments for A1/A2

- A2's fixture contract is platform-neutral at the source level. A3 found no
  hardware type leakage.
- A1 should confirm that the capabilities A2 lists (address space, sections,
  process/thread, exception delivery) remain opaque at the IF-009 boundary.
- A2 should add one sentence to IF-009: "Fixtures and the manifest are
  platform-neutral; no `o0_*` hardware or A3 device type appears in the loader
  contract."
- A2 should replace absolute manifest paths with repository-relative paths, or
  state that the manifest is generated in a fixed build root.
- A4 should keep XB-003 Open until A1's review and A2's revision are recorded.

## 4. What A3 does not review

- PE header parsing correctness, relocation coverage, TLS, unwind metadata,
  imports, CFG or CLR behaviour. Those are A2/A4 responsibilities.
- Loader runtime behaviour. A2's package is static-only.
- Any claim that a fixture runs under Ozero.

## 5. Status

A3 review is complete for the hardware boundary. A3 recommends **ACCEPT WITH
COMMENTS**: no blocking hardware-type issue; two documentation/reproducibility
comments for A2 and one registry update for A4.

## 6. References

- A2 PE loader requirements:
  <https://github.com/Momegaii/Ozero/blob/agent/a2-stage0/a2/docs/01-pe-loader-requirements.md>
- A2 fixture manifest:
  <https://github.com/Momegaii/Ozero/blob/agent/a2-stage0/a2/fixtures/build-manifest.json>
- A4 interface registry:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/architecture/interfaces.md>
- A4 cross-agent blockers:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/process/cross-agent-blockers.md>
- A1 conditions for IF-008:
  <https://github.com/Momegaii/Ozero/blob/a1/stage0-review/docs/a3-dma-contract.md>
