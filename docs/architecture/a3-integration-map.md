# A3 integration map — A4 candidate

| Field | Value |
| --- | --- |
| Merge commit | `44d566e972b729d3bf9557ab287833d095a7cdfb` |
| A3 source | PR #4 head `48d434f961ff0639a38ee50ba456e99d8ebb72da` |
| A4 parent before merge | `8b71be1ca3e1cf08bcebcd6c38a8356241f2bce2` |
| Files added | 56: 44 under `a3/`, 12 under `include/ozero/` |
| A4 control-plane changes | None outside `a3/` and `include/ozero/` |
| A1 root headers | 10 of 10 byte-identical to A1 `74b1471` |
| IF-008 headers | `platform.h` / `block.h` match `a3/include/ozero/` |
| Merge to `main` | **Not done** |

## Proof commands

```text
$ git diff --name-only 8b71be1 HEAD | wc -l
56
$ git diff --name-only 8b71be1 HEAD | grep -vE '^(a3/|include/ozero/)'
(none)
$ git ls-tree -r --name-only HEAD | grep -c '^a3/'
44
$ git ls-tree -r --name-only HEAD | grep -c '^include/ozero/'
12
```

## File-level ownership

| Status | Path | Owner |
| --- | --- | --- |
| A | `a3/.gitignore` | A3 (package metadata) |
| A | `a3/AGENTS.md` | A3 (package metadata) |
| A | `a3/EVIDENCE.md` | A3 (package metadata) |
| A | `a3/Makefile` | A3 (package metadata) |
| A | `a3/Ozero_A3_Action_Brief_2026-11-23_v2.pdf` | A3 (brief) |
| A | `a3/Ozero_A3_Briefing.pdf` | A3 (brief) |
| A | `a3/Ozero_A3_Proceed_Brief_2026-11-23.pdf` | A3 (brief) |
| A | `a3/Ozero_A3_Proceed_Brief_2026-11-23_R2.pdf` | A3 (brief) |
| A | `a3/README.md` | A3 (package metadata) |
| A | `a3/docs/01-hardware-support-matrix.md` | A3 (documentation) |
| A | `a3/docs/02-storage-architecture.md` | A3 (documentation) |
| A | `a3/docs/03-haswell-gen75-gpu.md` | A3 (documentation) |
| A | `a3/docs/04-driver-interface-draft.md` | A3 (documentation) |
| A | `a3/docs/05-hardware-test-plan.md` | A3 (documentation) |
| A | `a3/docs/06-stage0-review-response.md` | A3 (documentation) |
| A | `a3/docs/07-a3-action-brief-response.md` | A3 (documentation) |
| A | `a3/docs/08-control-plane-integration.md` | A3 (documentation) |
| A | `a3/docs/09-proceed-brief-response.md` | A3 (documentation) |
| A | `a3/docs/10-proceed-brief-r2-response.md` | A3 (documentation) |
| A | `a3/docs/decisions/a3-decision-requests.md` | A3 (documentation) |
| A | `a3/docs/interfaces/IF-008-dma-platform.md` | A3 (documentation) |
| A | `a3/docs/interfaces/IF-008-v1.1-a1-review-packet.md` | A3 (documentation) |
| A | `a3/docs/interfaces/IF-008-v1.1-acceptance.md` | A3 (documentation) |
| A | `a3/docs/interfaces/IF-009-a3-review.md` | A3 (documentation) |
| A | `a3/include/ozero/block.h` | A3 (package header copy) |
| A | `a3/include/ozero/boot.h` | A3 (package header copy) |
| A | `a3/include/ozero/cpu.h` | A3 (package header copy) |
| A | `a3/include/ozero/driver.h` | A3 (package header copy) |
| A | `a3/include/ozero/ipc.h` | A3 (package header copy) |
| A | `a3/include/ozero/irq.h` | A3 (package header copy) |
| A | `a3/include/ozero/mm.h` | A3 (package header copy) |
| A | `a3/include/ozero/o0.h` | A3 (package header copy) |
| A | `a3/include/ozero/platform.h` | A3 (package header copy) |
| A | `a3/include/ozero/status.h` | A3 (package header copy) |
| A | `a3/include/ozero/thread.h` | A3 (package header copy) |
| A | `a3/include/ozero/types.h` | A3 (package header copy) |
| A | `a3/matrix/hardware-support-matrix.csv` | A3 (evidence/matrix) |
| A | `a3/tests/a1_header_smoke.c` | A3 (tests/tools) |
| A | `a3/tests/header_smoke.c` | A3 (tests/tools) |
| A | `a3/tests/test_if008_contract.py` | A3 (tests/tools) |
| A | `a3/tests/test_matrix_semantics.py` | A3 (tests/tools) |
| A | `a3/tools/check_deliverables.py` | A3 (tests/tools) |
| A | `a3/tools/collect_inventory.sh` | A3 (tests/tools) |
| A | `a3/tools/validate_matrix.py` | A3 (tests/tools) |
| A | `include/ozero/block.h` | A3 (IF-008), A1-approved |
| A | `include/ozero/boot.h` | A1 (public header) |
| A | `include/ozero/cpu.h` | A1 (public header) |
| A | `include/ozero/driver.h` | A1 (public header) |
| A | `include/ozero/ipc.h` | A1 (public header) |
| A | `include/ozero/irq.h` | A1 (public header) |
| A | `include/ozero/mm.h` | A1 (public header) |
| A | `include/ozero/o0.h` | A1 (public header) |
| A | `include/ozero/platform.h` | A3 (IF-008), A1-approved |
| A | `include/ozero/status.h` | A1 (public header) |
| A | `include/ozero/thread.h` | A1 (public header) |
| A | `include/ozero/types.h` | A1 (public header) |

## Boundary

- The merge preserves A3 authorship in `a3/`; A4 only created the merge commit.
- Root `include/ozero/` contains A1’s ten headers unchanged plus A3’s two IF-008 headers.
- No A4 control-plane file, PR body, workflow or `CODEOWNERS` file was changed by the merge.
- A3’s package remains static-only. No AHCI probe, DMA transfer, GPU execution or hardware test ran.
- XB-020 may close only after the pushed A4 tree and PR #3 diff show these paths.
