# A3 integration verification — 2026-11-23

| Field | Value |
| --- | --- |
| A4 branch | `a4/stage0-review-followup` |
| Merge commit | `44d566e972b729d3bf9557ab287833d095a7cdfb` |
| A4 parent before merge | `8b71be1ca3e1cf08bcebcd6c38a8356241f2bce2` |
| A3 source | PR #4 head `48d434f961ff0639a38ee50ba456e99d8ebb72da` |
| Files added | 56: 44 under `a3/`, 12 under `include/ozero/` |
| A4 control-plane changes | None outside `a3/` and `include/ozero/` |
| A1 headers | 10/10 byte-identical to A1 `74b1471` |
| IF-008 headers | `platform.h` / `block.h` match `a3/include/ozero/` |
| Merge to `main` | **Not done** |
| XB-020 | Closed after tree proof |

The file-level map is [`docs/architecture/a3-integration-map.md`](../../docs/architecture/a3-integration-map.md).
The full command log is [`a3-integration-checks-2026-11-23.log`](a3-integration-checks-2026-11-23.log).

## Tree proof

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

## Header proof

```text
SAME boot.h
SAME cpu.h
SAME driver.h
SAME ipc.h
SAME irq.h
SAME mm.h
SAME o0.h
SAME status.h
SAME thread.h
SAME types.h
```

`include/ozero/platform.h` and `include/ozero/block.h` match `a3/include/ozero/` by blob
hash. The A1-owned headers were not overwritten.

## Checks

```text
$ make -C a3 check
...
Ran 19 tests in 0.004s
OK
exit=0

$ make -C a3 check-a1-headers A1_INCLUDE=include
cc -std=c11 -Wall -Wextra -Werror -Iinclude -Iinclude -fsyntax-only tests/header_smoke.c tests/a1_header_smoke.c
exit=0

$ make clean && make ci
...
test: ok (5 markers in order)
ci: ok
exit=0
```

## Boundaries

- This is integration into the A4 candidate branch, not a merge to `main`.
- PR #4 is not merged on GitHub; the merge commit is on the A4 branch.
- A3 remains static-only. No AHCI probe, DMA transfer, GPU execution or hardware test ran.
- D-007 and D-021 remain **Proposed**. The integration does not approve them.
- Stage 0 remains **NOT ACCEPTED**. D-006 remains **Proposed**.
