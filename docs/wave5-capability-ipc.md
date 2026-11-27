# Wave 5.0 — Capability handles, budgets and synchronous IPC

This work was originally numbered Wave 3. The Director's 2026-11-25 reorder moved
SMP and memory hardening to Wave 3 and platform services to Wave 5. The evidence is
retained here unchanged; the implementation is still single-CPU and emulator-only.

Scope: single CPU, emulator-verified only. Physical hardware is deferred. The
design is [ADR 0008](decisions/0008-capability-handles-budgets-and-ipc.md).

| Sub-wave | Scope | Status | Evidence |
|---|---|---|---|
| 5.0.1 | Refcounted object header, per-process handle tables, generation-tagged handle values, per-handle rights | **Done** | Host: `tests/cap_test.c` covers lookup, stale generations, rights rejection, duplicate/close and table teardown. Emulator: `OZERO: handle negative tests OK`. |
| 5.0.2 | Hierarchical budgets, charge/uncharge, split, exhaustion and accounting return to baseline | **Done** | Host: `cap_test` charges objects, pages and a deliberately tight budget; a partially charged allocation must unwind to `used == 0`. Emulator: `OZERO: budget exhaustion OK`. |
| 5.0.3 | Memory objects with budget-charged pages, read/map policy and W^X checks | **Done** | Host: `cap_test` creates, reads, validates and destroys memory objects, including the partial-failure path. Emulator: the `handles` program exercises `memory_create`, `memory_map` and refusal of foreign-budget mappings. |
| 5.0.4 | Endpoints, synchronous send/receive/call, handle transfer and peer-death wakeups | **Done** | Host: `cap_test` covers endpoint creation and destruction. Emulator: `OZERO: ipc pair OK pair-1`, `OZERO: ipc negative tests OK`, and the `peer-server` test. |
| 5.0.5 | `process_spawn` syscall, grant installation and detached child reaping | **Done** | Emulator: `OZERO: spawn syscall OK`, `OZERO: wave3 done spawner status=0`. |
| 5.0.6 | Negative tests: forged/stale handles, rights escalation, foreign budget, bad IPC pointers, dead peers, budget exhaustion and process churn | **Done** | Host: `cap_test` rejects forged handles and rights escalation. Emulator: `handles`, `budget`, `ipcneg` and eight churn pairs; `OZERO: wave3 churn OK pairs=8` requires object count, budget used and PMM free pages to return to baseline. |
| 5.0.7 | Verification on the full BIOS/UEFI × `qemu64`/`Haswell-v1` matrix plus the sanitizer job; STATUS evidence by type | **Done** | All four cells pass locally on QEMU 10.0.13; `make test-sanitize` passes with leaks enabled. CI on commit `521bc04` completed SUCCESS with all five jobs on QEMU 10.2.1 (run number not independently verified in this workspace); the capability/IPC markers are present in the CI job logs. `tools/qemu-smoke.sh` requires those markers. Physical hardware has not been exercised. |

Wave 5.0 verification detail:

- **Host tests:** `make test` and `make test-sanitize` pass, including `cap_test`.
- **Emulator:** `{bios, uefi} × {qemu64, Haswell-v1}` on QEMU 10.0.13 with the full
  Wave 2 and capability/IPC marker sets and `QEMU smoke test: PASS` in every cell.
- **CI:** CI on commit `521bc04` completed SUCCESS with all five jobs (`boot` × 4 and `host-sanitizers`) on QEMU 10.2.1 with OVMF (run number not independently verified in this workspace). The job logs contain the capability/IPC markers.
- **Physical hardware:** never exercised. No Wave 5.0 result is physical evidence.
- **Defects found and fixed while closing the wave:** a partially charged memory object
  uncharged only the pages it had allocated, not the full page array (24-byte leak per
  failed create); `o0_ipc_call` took an extra reference on its reply endpoint and leaked one
  endpoint per call. Both are covered by the host partial-failure test and the emulator
  churn check respectively.

The wave is signed off for its emulator scope only. SMP, preemption, SMEP/SMAP, KASLR,
signals, asynchronous IPC, `mmap`, dynamic linking and `fork` remain deferred.

