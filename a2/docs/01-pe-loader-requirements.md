# PE loader requirements (PE32/PE32+)

- Status: draft ready (requirements and measured image survey; no loader exists)
- Evidence: [data/pe32plus-structures.json](../data/pe32plus-structures.json); `python3 tools/pe_inspect.py build/*.exe build/*.dll .cache/*`; `python3 tools/pe_structs_check.py ...` reports 263 checks, 0 failed; raw output in [evidence/pe-structure-checks.txt](evidence/pe-structure-checks.txt) and [evidence/pe-inspect.txt](evidence/pe-inspect.txt)
- Decisions needed: whether 32-bit images are required at Stage 7 (Q2); whether the layer ships its own `ntdll` or consumes the Windows one (Q4); CFG and signature enforcement policy (Q7)
- Risks: spec gaps around load config and the CLR header; malformed-image handling; treating parsing as loading; see [05-risk-report.md](05-risk-report.md)
- Next step: implement the M0 "dry-run loader" against the fixture set and record L1 results, per [07-stage7-milestone.md](07-stage7-milestone.md)

## Scope of this document

This document lists what a user-mode loader must read and what it must then do,
for PE32 and PE32+ images. It separates three activities that are often conflated:

| Activity | Definition | Output |
| --- | --- | --- |
| Parse | read structures from the file and validate them | a description of the image |
| Map | reserve address space, map sections, apply relocations, resolve imports | a loadable image at an address |
| Run | transfer control to the entry point with a valid process, thread, TEB, PEB and stack | application behaviour |

`tools/pe_inspect.py` performs the first activity only. That is deliberate: the
repository can prove parsing facts today without claiming that anything runs.

## Header layout that the loader depends on

All offsets below are documented in the Microsoft PE specification and encoded
in [data/pe32plus-structures.json](../data/pe32plus-structures.json). The JSON is
checked against real images, not only against prose.

### DOS header and COFF file header

| Structure | Size | Fields the loader uses |
| --- | --- | --- |
| `IMAGE_DOS_HEADER` | 64 | `e_magic` (0x5A4D), `e_lfanew` at 0x3C: file offset of `PE\0\0` |
| `IMAGE_FILE_HEADER` | 20 | `Machine`, `NumberOfSections`, `SizeOfOptionalHeader`, `Characteristics` (`IMAGE_FILE_DLL` 0x2000, `IMAGE_FILE_RELOCS_STRIPPED` 0x0001) |

### Optional header (PE32+)

| Offset | Size | Field | Loader relevance |
| --- | --- | --- | --- |
| 0 | 2 | `Magic` | 0x20B = PE32+, 0x10B = PE32 |
| 16 | 4 | `AddressOfEntryPoint` | RVA of the first instruction |
| 24 | 8 | `ImageBase` | Preferred base; must be 64 KiB aligned |
| 32 | 4 | `SectionAlignment` | In-memory alignment |
| 36 | 4 | `FileAlignment` | On-disk alignment, power of two, 512–64 KiB |
| 56 | 4 | `SizeOfImage` | Bytes to reserve; multiple of `SectionAlignment` |
| 60 | 4 | `SizeOfHeaders` | Bytes of headers to map; multiple of `FileAlignment` |
| 64 | 4 | `CheckSum` | Spec gives the algorithm only as "incorporated into IMAGHELP.DLL" |
| 68 | 2 | `Subsystem` | 2 GUI, 3 CUI, 1 native; selects the startup path |
| 70 | 2 | `DllCharacteristics` | `DYNAMIC_BASE` 0x40 (ASLR), `NX_COMPAT` 0x100, `GUARD_CF` 0x4000, `HIGH_ENTROPY_VA` 0x20 |
| 72..104 | 8 each | stack/heap reserve and commit | Thread and process defaults |
| 108 | 4 | `NumberOfRvaAndSizes` | Count of 8-byte data directory entries |
| 112 | 8 × N | `DataDirectory[]` | Table locations |

The measured invariant is `SizeOfOptionalHeader == 112 + 8 × NumberOfRvaAndSizes`,
which is 240 for the usual 16 directories. All eight images inspected in this
repository satisfy it. For PE32 the corresponding fixed part is 96 bytes and the
directories start at 96, so `SizeOfOptionalHeader == 96 + 8 × N` (224 for 16
directories); 7-Zip and Newtonsoft.Json confirm this.

### Data directory indices

| Index | Name | Index | Name |
| --- | --- | --- | --- |
| 0 | export | 8 | global_ptr |
| 1 | import | 9 | TLS |
| 2 | resource | 10 | load config |
| 3 | exception | 11 | bound import |
| 4 | certificate (file offset, not RVA) | 12 | IAT |
| 5 | base relocation | 13 | delay import |
| 6 | debug | 14 | CLR runtime header |
| 7 | architecture (reserved) | 15 | reserved |

### Section header

40 bytes: `Name[8]`, `VirtualSize`, `VirtualAddress`, `SizeOfRawData`,
`PointerToRawData`, `PointerToRelocations`, `PointerToLinenumbers`,
`NumberOfRelocations`, `NumberOfLinenumbers`, `Characteristics`.

The specification requires image sections to be ordered by ascending RVA,
adjacent, and aligned to `SectionAlignment`. The checker enforces both rules; all
fixtures and samples pass, which is a useful signal that the documented section
layout is being read correctly.

## Loader pipeline

Each step lists the observable failure mode, because a compatibility layer needs
to report something better than "crash".

| # | Step | Inputs | Required behaviour | Failure mode |
| --- | --- | --- | --- | --- |
| 1 | Validate | DOS, PE, COFF, optional magic, sizes | reject out-of-bounds or contradictory headers before allocating | structured error, no partial state |
| 2 | Select format | `Magic`, `Machine` | PE32+ x64 first; PE32 only if 32-bit support exists | explicit "unsupported machine" |
| 3 | Reserve image | `SizeOfImage`, `ImageBase`, `DllCharacteristics` | reserve a contiguous range, honour ASLR if `DYNAMIC_BASE` | address-space exhaustion |
| 4 | Map headers | `SizeOfHeaders` | map headers read-only, zero-fill the rest | mapping failure |
| 5 | Map sections | section table, `Characteristics` | apply `PAGE_EXECUTE_READ`, `READWRITE`, `READONLY`; zero-fill `VirtualSize > SizeOfRawData` | protection mismatch |
| 6 | Relocate | `.reloc`, delta | apply `DIR64` (x64) / `HIGHLOW` (x86) entries; skip `ABSOLUTE`; honour `RELOCS_STRIPPED` | image cannot load at chosen base |
| 7 | Resolve imports | import descriptors, thunk arrays | load each DLL, resolve by name or ordinal, write IAT, handle forwarders | missing export, loader recursion |
| 8 | Delay imports | delay descriptors | install a stub; resolve on first call | stub not patched |
| 9 | TLS | TLS directory | allocate a TLS index, copy the template for the initial thread, queue callbacks | callbacks run too late |
| 10 | Exception metadata | exception directory (x64) | register `RUNTIME_FUNCTION` entries with the unwinder | no stack unwinding, broken debugger |
| 11 | Load config | load config directory | publish the security cookie; either honour CFG metadata or state that CFG is not enforced | CFG silently disabled |
| 12 | Start | `AddressOfEntryPoint`, subsystem | call TLS callbacks, then `DllMain`/entry point with the documented loader-lock rules | deadlock in loader lock |
| 13 | Notify | loader notifications | deliver load notifications to registered callbacks | plugins do not initialise |
| 14 | Unload | `FreeLibrary` | run `DllMain(DLL_PROCESS_DETACH)`, TLS destructors, unmap | leaks |

### Imports: details that bite

- `OriginalFirstThunk` (import lookup table) may be zero; then the IAT itself is
  the lookup source. A loader that assumes the lookup table always exists will
  misread bound or hand-crafted images.
- Bit 63 of a 64-bit thunk marks import by ordinal; the low 16 bits are the
  ordinal. Otherwise the value is an RVA of `IMAGE_IMPORT_BY_NAME` (hint + name).
- The descriptor table is terminated by an all-zero 20-byte descriptor. The
  checker verifies that a terminator exists inside the directory; a loader must
  bound the scan by the directory size anyway.
- Export forwarders are encoded as an export RVA that points inside the export
  directory itself; the target is a string such as `NTDLL.RtlAllocateHeap`.
- Bound imports record a timestamp; a mismatch means normal resolution is
  required. This is a correctness path, not an optimisation detail.

### Relocations

- Blocks are per 4 KiB page: `PageRVA`, `BlockSize`, then 2-byte entries whose
  high 4 bits are the type and low 12 bits are the offset.
- AMD64 uses `DIR64` (10). 32-bit images use `HIGHLOW` (3). `ABSOLUTE` (0) is
  padding and must be skipped.
- The specification notes that the loader need not process relocations unless the
  image cannot be loaded at its preferred base. With ASLR enabled, that is the
  normal case, so relocation support is not optional for a modern baseline.

### TLS

- `IMAGE_TLS_DIRECTORY64` holds **VAs**, not RVAs, for the template start/end,
  the index slot and the callback array. They must be relocated like any other
  address.
- The callback array is null-terminated and is invoked with the same signature as
  `DllMain`. Ordering matters: TLS callbacks run before the entry point, and the
  process-attach callback runs before `DllMain`.
- The PE specification notes that static TLS in dynamically loaded DLLs was only
  made reliable from Windows Vista onward; a compatibility layer should target
  the post-Vista behaviour.

### Exception and unwind metadata

The PE specification's exception directory is only a pointer and size. The actual
layout is defined in the x64 exception-handling documentation:

- `.pdata` is an array of 12-byte `RUNTIME_FUNCTION` entries (begin RVA, end RVA,
  unwind-info RVA), sorted and DWORD-aligned.
- `UNWIND_INFO` starts with a version/flags byte, prolog size, unwind-code count
  and frame register byte; version is currently 1.
- Flags: `UNW_FLAG_EHANDLER` (1), `UNW_FLAG_UHANDLER` (2), `UNW_FLAG_CHAININFO` (4).
  Chained entries point at another `RUNTIME_FUNCTION`.

The measured sample set shows all three flag combinations in real code
(`markupsafe` has 8 chained entries, 2 exception handlers and 2 termination
handlers among 40 entries). A loader that ignores chained info will unwind
incorrectly on ordinary release binaries.

### Load configuration and Control Flow Guard

`IMAGE_LOAD_CONFIG_DIRECTORY64` is version-tagged by its first field, `Size`.
The measured real-world sample declares 320 bytes and sets `GuardFlags` to 0x100
(`IMAGE_GUARD_CF_INSTRUMENTED`).

Two facts follow:

1. The loader must read the structure according to its declared size, never
   according to the data directory size alone.
2. CFG is a policy decision, not a parsing detail. If the layer does not enforce
   CFG, it must say so, because a security claim would otherwise be false.

### CLR images

For a managed image, data directory 14 points at a 72-byte `IMAGE_COR20_HEADER`.
The Microsoft PE specification only defines the object-file `.cormeta` section and
points the data directory there, so the image-level layout comes from ECMA-335.
The Windows behaviour is documented separately: the loader detects the managed
bit, loads `mscoree.dll`, and `_CorValidateImage` rewrites the entry point and, on
64-bit Windows, transforms the image from PE32 to PE32+ in memory.

That transformation is a loader-level behaviour, not a CLR detail. A compatibility
layer that hosts the CLR must either reproduce it or use a runtime that accepts
the original image.

## Structure inventory summary

| Structure | Size (bytes) | Source | Loader role |
| --- | --- | --- | --- |
| `IMAGE_DOS_HEADER` | 64 | PE spec | find PE signature |
| `IMAGE_FILE_HEADER` | 20 | PE spec | machine, section count, optional-header size |
| `IMAGE_OPTIONAL_HEADER64` | 112 + 8N | PE spec | base, alignment, entry, subsystem, directories |
| `IMAGE_DATA_DIRECTORY` | 8 | PE spec | table location |
| `IMAGE_SECTION_HEADER` | 40 | PE spec | map file ranges to RVAs |
| `IMAGE_IMPORT_DESCRIPTOR` | 20 | PE spec | per-DLL import records |
| `IMAGE_THUNK_DATA64` | 8 | PE spec | name/ordinal lookup entries |
| `IMAGE_EXPORT_DIRECTORY` | 40 | PE spec | export tables and forwarders |
| `IMAGE_BASE_RELOCATION` | 8 + 2n | PE spec | per-page fixups |
| `IMAGE_TLS_DIRECTORY64` | 40 | PE spec | TLS template and callbacks |
| `RUNTIME_FUNCTION` | 12 | x64 unwind doc | .pdata entry |
| `UNWIND_INFO` | 4 + 2n | x64 unwind doc | prolog/unwind description |
| `IMAGE_LOAD_CONFIG_DIRECTORY64` | versioned, 320 observed | winnt.h | cookie, SEH table, CFG |
| `IMAGE_DELAYLOAD_DESCRIPTOR` | 32 | PE spec | load-on-first-call DLLs |
| `IMAGE_BOUND_IMPORT_DESCRIPTOR` | 8 | PE spec | bound timestamps |
| `IMAGE_DEBUG_DIRECTORY` | 28 | PE spec | symbols, reproducible builds |
| `IMAGE_RESOURCE_DIRECTORY` | 16 | PE spec | resource tree |
| `IMAGE_RESOURCE_DIRECTORY_ENTRY` | 8 | PE spec | tree edges |
| `IMAGE_RESOURCE_DATA_ENTRY` | 16 | PE spec | resource bytes |
| `IMAGE_COR20_HEADER` | 72 | ECMA-335 | managed image |
| `WIN_CERTIFICATE` | 8 + payload | PE spec | Authenticode |

Full field-level detail, including citations and caveats, is in
[data/pe32plus-structures.json](../data/pe32plus-structures.json).

## Measured evidence from this repository

Eight images were parsed: five locally built fixtures and three third-party
binaries. Nothing was executed.

| Image | Format | Imports | Relocations | .pdata | TLS | Other |
| --- | --- | --- | --- | --- | --- | --- |
| `console_hello.exe` | PE32+ CUI | 7 DLLs / 38 symbols | 32 DIR64 | 31 | 8 B, 2 callbacks | — |
| `fileproc.exe` | PE32+ CUI | 7 / 49 | 32 DIR64 | 31 | 8 B, 2 callbacks | — |
| `gui_messagebox.exe` | PE32+ GUI | 8 / 41 | 32 DIR64 | 31 | 8 B, 2 callbacks | subsystem 2 |
| `tls_dll.dll` | PE32+ DLL | 4 / 20 | 26 (23 DIR64) | 23 | 16 B, 3 callbacks | 2 exports |
| `cpp_throw.exe` | PE32+ CUI | 9 / 52 | 1410 (1404 DIR64) | 470 | 24 B, 2 callbacks | C++ EH |
| `7zr.exe` | PE32 i386 CUI | 5 / 139 | 9546 (9484 HIGHLOW) | 0 | none | resources |
| `Newtonsoft.Json.dll` | PE32 IL-only | 1 / 1 (`mscoree`) | 2 | 0 | none | CLR header, Authenticode |
| `_speedups.pyd` | PE32+ DLL | 4 / 20 | 22 (20 DIR64) | 40 (8 chained) | none | load config, CFG |

Two findings matter for planning:

1. Even the smallest fixture needs API set resolution. The mingw/UCRT toolchain
   imports `api-ms-win-crt-*` contracts (6 distinct contracts, 27 symbols), not
   a single `msvcrt.dll`. A loader that only resolves literal DLL names fails at
   `hello world`.
2. `kernel32.dll` dominates the import graph (162 of 360 symbols across the
   eight-image sample), followed by the CRT contracts (8 distinct contracts,
   127 symbols across the five fixtures). That is the first implementation
   surface, not `user32`.

## Static evidence and its limits

The checks in this repository are L0 evidence: they prove that the documented
layout is read correctly from real files, not that a loader would behave like
Windows. Sample hashes, exact offsets and raw bytes for the API set, TLS,
relocation, chained-unwind and load-config findings are in
[10-technical-findings.md](10-technical-findings.md). Loader behaviour is tested
only from M0 onward.

## Specification gaps and discrepancies

These are recorded so that implementation does not inherit false confidence.

| # | Gap | Consequence | Mitigation |
| --- | --- | --- | --- |
| G1 | The specification says it "is not guaranteed to be a complete specification in all respects" | undocumented fields and behaviour exist | treat Windows binaries and headers as additional oracles; record every deviation |
| G2 | The load-config layout table in the specification does not match `winnt.h` (it starts at `Characteristics` and omits `Size`) | misreading `GuardFlags` and the cookie | follow `winnt.h`; validate declared `Size` (320 observed) |
| G3 | The CLR header is only described as an object-file `.cormeta` section | the image-level layout is unspecified | use ECMA-335; test against a real assembly |
| G4 | `.pdata`/`UNWIND_INFO` are not in the PE specification at all | exception handling looks trivial on paper | use the x64 exception-handling documentation; handle chained info |
| G5 | The checksum algorithm is not specified ("incorporated into IMAGHELP.DLL") | cannot validate checksums from the document alone | treat as optional; do not claim validation |
| G6 | Delay-load descriptor semantics (RVA vs VA by `Attributes` bit 0) are thinly documented | half the descriptors are misread | implement both modes; test with a synthetic descriptor |
| G7 | Bound-import behaviour on timestamp mismatch is described only in outline | silent misresolution | verify against documented loader behaviour; treat binding as optional |
| G8 | DLL search order is specified in a different document, and includes API sets, KnownDLLs and SxS redirection | "load the DLL next to the EXE" is wrong | implement the documented order; see [02-nt-win32-dependency-map.md](02-nt-win32-dependency-map.md) |
| G9 | The specification warns not to assume that directory RVAs point to section starts or that sections have specific names | name-based heuristics break | resolve by RVA only |

## Robustness requirements for parsing untrusted input

A compatibility layer will be fed arbitrary binaries. The parser is part of the
attack surface.

- Bounds-check every read; never trust `SizeOfOptionalHeader`, `NumberOfSections`,
  `NumberOfRvaAndSizes` or directory sizes without validation.
- Cap descriptor and thunk scans (the inspector caps at 4096 descriptors and
  65 536 thunks) and report truncation rather than looping.
- Reject overlapping or non-ascending sections; reject entry points outside an
  executable section.
- Never map a section from a file offset that is outside the file, and never
  treat the certificate directory as an RVA.
- Fuzz the parser before the loader exists; the current tool is a suitable target.

## What the loader does not need

- COFF symbol table and string table (18-byte records) for images.
- Line-number tables, `.drectve`, `.debug$S`, `.debug$T` (object files only).
- `.sxdata` (x86 object files only).
- Rich header decoding. It is useful for provenance, not for loading.

## Evidence

```
$ python3 tools/pe_structs_check.py build/*.exe build/*.dll .cache/7zr.exe ...
230 checks, 0 failed
```

The check set includes: optional-header size vs `NumberOfRvaAndSizes`, alignment
rules for image base/sections/headers, sorted and adjacent sections, entry point
inside an executable section, every directory RVA mapped, `.pdata` entry size and
unwind versions, relocation block consistency, import terminator presence, TLS
callback termination, CLR header size and load-config size sanity. A wrong offset
in the inventory fails these checks, so the result is evidence about the
documented layout, not only about the tool.

## Sources

- Microsoft, PE format specification: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Unwind data structures for x64: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>
- Microsoft, Dynamic-link library search order: <https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order>
- Microsoft, Windows API sets: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>
- Microsoft, Managed execution process (`_CorValidateImage`): <https://learn.microsoft.com/en-us/dotnet/standard/managed-execution-process>
- ECMA-335 (CLI header): <https://ecma-international.org/publications-and-standards/standards/ecma-335/>
- Intel 64 and IA-32 Architectures Software Developer Manuals: <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
