# A1 Stage 0 raw evidence

| Field | Value |
| --- | --- |
| Source branch | `a1/stage0-review` |
| Source commit | `2693a34a058506e4755948989fb420c9a20a91ea` |
| Source path | `docs/evidence/a1-raw-checks-2026-11-23-r2.txt` |
| Source blob | `24ce6a9a1ae3fd0284ddcce36592f3fe05b2a0e3` |
| Copied file | `a1-raw-checks-2026-11-23-r2.txt` |
| Copy SHA-256 | `457235e5aa1c5a8c23a6efcd4eee6dc652c8264263da12d624c84d4d5cbd467e` |
| A4 verification | `a4-verification-2026-11-23.txt` |
| Verification SHA-256 | `1c2349f96091831a0f4aa67b6ba12e221b3ecb4f7cda90334a37b1b2897aa432` |
| Verified at | A1 `2693a34`, 2026-11-23T14:24:42Z |

The copied file is A1's raw log, preserved verbatim. It was generated at A1 `74b1471` and
records the commands and output for that review round. The A4 verification file re-runs the
header and ABI conformance script at the live A1 head `2693a34` and records the tool
versions, exit codes and full output.

## Verification result

```text
$ ./tools/check-headers.sh
...
RESULT: PASS
exit=0
```

The verification covers standalone C11 compilation of all ten A1 headers, the umbrella
header in C11 and C++17, the ABI static assertions, generic-interface device neutrality,
freestanding-only headers and the `include/ozero` namespace. It does not test kernel
runtime behavior.

## Provenance rules

- A1's authorship and commit history are unchanged.
- This directory is an A4 evidence copy, not a rewrite of A1's branch.
- The raw log's embedded refs stop at `b4232a0`; that is historical. The live A1 head is
  `2693a34`, and the A4 verification file records it.
- XB-016 may close because the raw command output is committed and independently verified.
  The log does not prove boot, hardware, PE execution or Windows compatibility.
