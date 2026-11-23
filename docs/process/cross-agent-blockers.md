# Cross-agent blocker register

One row per dependency between agents. A dependent implementation does not merge until the owner
signs the interface and the evidence is linked here. Status words are **Open**, **Signed** or
**Closed**; only the named owner can move a row to Signed.

| ID | From → to | Dependency | State | Evidence or next action |
| --- | --- | --- | --- | --- |
| XB-001 | A1 → A3 | A1 reviews the A3 DMA/platform/block interface (IF-008) before any AHCI probe work | **Open** | A3 posts the interface version and probe plan; A1 records approval on the PR |
| XB-002 | A1 → A2 | Process, address-space, handle and syscall contracts match A1's kernel contracts | **Open** | A1 publishes the contract versions; A2 links them from IF-002 and IF-003 |
| XB-003 | A2 → A1/A3 | PE fixture contract (IF-009) is fixed before any PE test | **Open** | A2 posts IF-009; findings stay static-only until a sandboxed runtime job exists |
| XB-004 | A1 → A4 | Limine pin, boot protocol and kernel handoff (IF-001, D-005) | **Open** | A1 records the exact Limine commit and handoff; A4 adds `future-limine-kernel-boot` |
| XB-005 | A3 → A4 | Device-specific tests and hardware requirements are named with evidence | **Open** | A3 links each device claim to a test target; no hardware claim without a run |
| XB-006 | A4 → Director | Publish the A4 branch and open the Stage 0 review PR (D-008) | **Open** | Needs a GitHub credential with Contents: write; current token is read-only |

## Rules

- The MBR harness target (`mbr-harness-smoke`) never satisfies XB-004. It is BIOS-only and does not
  load Limine or an Ozero kernel.
- A2's PE findings remain static-only. A runtime corpus is an opt-in, sandboxed job and is not
  normal CI.
- A3 does not start AHCI probe work while XB-001 is Open.
- A status change needs a commit, a command and raw output. A link to a branch alone is not evidence.
- The Director decides XB-006. A4 cannot grant itself publish rights.
