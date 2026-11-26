# Stage 7 — Acceptance and verification plan

**Status:** Planned; no Stage 7 runtime acceptance is claimed.  
**Last updated:** 2026-11-27  
**Parent:** [stage7.md](stage7.md)  
**Architecture:** [ADR 0021](decisions/0021-pe-loader-windows-user-mode-abi.md)

## Evidence labels

Use these labels in every report:

- **Source present** — code exists; no behavior is implied.
- **Host-tested** — named host test completed successfully.
- **Sanitizer-tested** — ASan/UBSan run completed successfully.
- **Fuzzed** — state corpus, engine, duration/iterations and coverage approach; deterministic random tests must not be called coverage-guided fuzzing.
- **QEMU-verified** — exact QEMU version, firmware, CPU, SMP count and serial evidence recorded.
- **Physical** — exact hardware and observed evidence; never infer from QEMU.

## Gate A — PE parser

- [ ] Valid PE32+ AMD64 console image accepted.
- [ ] Reject bad DOS magic, NT signature, machine, optional-header magic and truncated structures.
- [ ] Reject invalid section count, alignment, raw-data bounds, virtual bounds and overlapping sections.
- [ ] Reject invalid entry point and entry point in non-executable section.
- [ ] Reject W+X sections and images exceeding configured limits.
- [ ] Validate every supported data-directory range and reject malformed directory pairs.
- [ ] Validate relocation blocks and reject unsupported relocation types.
- [ ] Validate exception-directory bounds; do not assume metadata is safe because the PE header parsed.
- [ ] Deterministic fuzz/mutation corpus passes under ASan/UBSan.
- [ ] Coverage-guided fuzz target runs for a recorded duration/corpus and retains regressions.

**Required artifacts:** host test output, sanitizer output, fuzz command/seed/corpus summary, and parser status-code matrix.

## Gate B — image loader

- [ ] Map image into an isolated Ozero user address space.
- [ ] Apply section permissions with W^X and NX enforcement.
- [ ] Zero-fill virtual tails and map headers only according to the approved policy.
- [ ] Apply supported DIR64 base relocations after bounds-checking every target.
- [ ] Validate chosen base and image end against the user image window.
- [ ] Construct Windows x64-compatible initial stack and entry state.
- [ ] Roll back every partial mapping/allocation on failure.
- [ ] Ensure malformed image cannot publish a partially initialized process.
- [ ] Verify image teardown returns page/object accounting to baseline.

**Required artifacts:** loader unit tests, allocation-failure tests, page-permission assertions, and QEMU malformed-image rejection.

## Gate C — console hello-world

- [ ] Build PE32+ console test with static compatibility runtime.
- [ ] Verify PE entry reaches ring 3.
- [ ] Verify standard-output handle and `WriteFile` route to the native output primitive.
- [ ] Verify exact expected output and process exit status.
- [ ] Verify invalid handle, bad pointer and non-zero exit paths.
- [ ] Record `OZERO: stage7 pe hello OK` only after all assertions pass.

**Required artifacts:** PE binary hash, build recipe, serial log, QEMU command/configuration and exit status.

## Gate D — files, handles and synchronization

- [ ] Create/open/read/write/seek/close a file through Win32 wrappers and capability VFS.
- [ ] Check permissions, invalid paths, short reads/writes and offset boundaries.
- [ ] Check stale and duplicated handles, double-close and process-exit cleanup.
- [ ] Test mutex/event/critical-section wait, signal, timeout and teardown behavior.
- [ ] Verify no Win32 handle value or NTSTATUS leaks into the kernel ABI.
- [ ] Verify object and PMM accounting returns to baseline after churn.

**Required artifacts:** host tests, sanitizer tests, PE integration program, serial log and filesystem read-back.

## Gate E — threads

- [ ] Verify native user-thread syscall/contract is approved before Win32 `CreateThread` is implemented.
- [ ] Verify each thread starts with correct x64 register and stack state.
- [ ] Verify wait, completion, timeout and exit status.
- [ ] Verify synchronization under concurrent execution.
- [ ] Verify cleanup after thread/process termination.
- [ ] Explicitly report BSP pinning; no user-thread SMP claim until separately verified.

**Required artifacts:** thread lifecycle tests, QEMU multi-thread sample, scheduler/CPU configuration and resource accounting.

## Gate F — SEH and registry

- [ ] Reject malformed `.pdata`/`.xdata` before execution.
- [ ] Test each explicitly supported exception class and handler path.
- [ ] Test unsupported exception and unwind behavior.
- [ ] Test registry stub create/open/query/set/close only for the approved subset.
- [ ] Test invalid types, oversized values, unsupported keys and deterministic error returns.
- [ ] Confirm no persistence claim unless disk-backed behavior is implemented and tested.

**Required artifacts:** PE fixtures, host tests, QEMU output and supported/unsupported API table.

## Gate G — real small program

- [ ] Record program name, source URL/license, binary hash and compiler/linker.
- [ ] Enumerate imported symbols and runtime dependencies.
- [ ] Map every import to implemented/partial/stubbed/unsupported status.
- [ ] Run from Ozero filesystem; verify file access, synchronization and exit status as applicable.
- [ ] Repeat the run and compare deterministic output.
- [ ] Publish known limitations and any API-specific workarounds.

**Required artifacts:** source/build instructions, binary hash, import inventory, serial logs, test matrix and compatibility report.

## Minimum QEMU matrix

Start with the existing project boot matrix and expand only when stable:

| Firmware | CPU model | SMP | Purpose |
|---|---|---:|---|
| BIOS | qemu64 | 1 | Fast development smoke |
| UEFI/OVMF | qemu64 | 1 | Firmware path |
| BIOS | Haswell-v1 | 1 | Feature-baseline coverage |
| UEFI/OVMF | Haswell-v1 | 1 | Combined firmware/CPU coverage |

Add SMP 2 only after the user-thread/per-CPU TSS/IST prerequisites are met. Record exact QEMU and OVMF versions. A green host test is not a substitute for any QEMU gate.

## Release rule

A milestone is complete only when its source, tests and required evidence all exist on the reviewed commit. Never mark a milestone complete because a header, parser, stub or build target exists.
