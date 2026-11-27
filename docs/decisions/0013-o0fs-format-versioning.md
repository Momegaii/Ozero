# ADR 0013: O0FS on-disk format revision and versioning

- **Status:** Accepted (design only; nothing implemented). Director approved 2026-11-25.
- **Date:** 2026-11-25
- **Wave:** 4.0 (design), enforced from 4.5
- **Related:** ADR 0003 (Stage 4 storage substrate), ADR 0012 (crash consistency),
  `docs/o0fs-format.md`

## 1. Context

`docs/o0fs-format.md` is a proposal, not an approved format. Its superblock carries:

| Field | Today |
|---|---|
| `magic` | `0x5346304F` ("O0FS") |
| `version` | `1`, a `uint16_t` |
| `block_size` | 4096 |
| `flags` | 0, reserved |
| `checksum` | 32-bit FNV-1a over bytes `[0, 52)` |

Mounting requires `magic` and `version` to match the compiled constants exactly, and
rejects any other value. That is safe today because there is exactly one version, but it
has two problems for Wave 4:

1. A future reader cannot tell "this volume is newer than me" from "this volume is
   corrupt"; both are refused identically.
2. Wave 4.5 adds a journal region and allocator state to the superblock (ADR 0012), so
   version 1 images will exist and must be handled deliberately, not by silently
   reinterpreting reserved bytes.

There is no installed base and no upgrade tool, so the policy can be chosen now rather
than after a mistake.

## 2. Alternatives

| Alternative | Trade-off |
|---|---|
| **Major/minor in the existing 16-bit field plus a feature-bit word (chosen)** | No struct growth: split `version` into major (high byte) and minor (low byte), and use the reserved `flags` word for compatibility classes. A reader refuses unknown incompatible features, ignores unknown read-only-compatible ones, and can explain *why* it refused. |
| One monotonic integer, exact match required | Simplest, and it is what exists. But every additive change forces a full version bump, and a newer minor revision becomes unreadable with no diagnostic beyond "wrong version". |
| Free-form feature string | Flexible and human-readable, but needs string parsing, fixed maximum length and a duplicate-name policy. Overkill for a 16 MiB volume format. |
| Never version; keep reserved bytes for future use | Guarantees silent misreads the first time a reserved byte becomes meaningful. Rejected. |
| Auto-migrate on mount | Convenient, but a mount that rewrites the format is a write path that must itself be crash-safe (ADR 0012), and it destroys the ability to inspect the old image. Rejected: migration is an explicit offline operation. |

## 3. Decision

1. **Version layout.** `version` stays `uint16_t`: high byte = major, low byte = minor.
   Version 1 today is `0x0001`; the journal format of ADR 0012 is `0x0002`.
2. **Mount rules.**
   - `magic` mismatch → refuse (`O0_EINVAL`-class), "not an O0FS volume".
   - major newer than the kernel → refuse with an explicit "volume format newer than
     this kernel" status. Never attempt to read it.
   - major older than the kernel → refuse read-write; read-only mounting is allowed only
     if no incompatible feature bit is set (proposal: version 1 is not read-only
     mountable by the version 2 reader, because the superblock layout changed).
   - minor newer, same major → allowed read-only if all *read-only-compatible* feature
     bits are understood or explicitly ignorable; refused for write.
3. **Feature word.** The reserved `flags` field becomes three bit ranges:
   `compat` (safe to ignore for both read and write), `ro_compat` (safe to ignore for
   read only), `incompat` (must refuse if unknown). The exact bit assignment is fixed
   when each feature lands; the ranges are part of the format from version 2 onward.
4. **Reserved bytes are checked.** Reserved regions must be zero; a nonzero reserved
   byte is treated as an unknown incompatible feature, not ignored. This is what makes
   "reserved" mean something.
5. **Checksum covers the decisions.** The superblock checksum covers `magic`, `version`,
   `block_size`, the feature word and every layout field — that is, all bytes before the
   checksum field, as today. The checksum algorithm itself is named by a small
   algorithm id stored alongside it (proposal: id 1 = FNV-1a, id 2 = CRC32C once
   chosen), so the algorithm can change without another format break.
6. **No silent upgrade, ever.** Migration between majors is an explicit operation that
   reads the old volume, writes a new one and verifies it. It is not part of mount, and
   the kernel never rewrites a version it does not fully understand.
7. **The format stays a proposal until approved.** This ADR defines the versioning
   mechanism, not the final format. `docs/o0fs-format.md` keeps its proposal status.

## 4. Consequences

- Version 1 volumes become explicitly unreadable by the version 2 implementation (the
  superblock layout changes), which is acceptable because no shipped image exists; the
  refusal must be a clear diagnostic, not a generic mount failure.
- Every future on-disk change must declare a compatibility class. A change that only
  adds ignorable data is a minor bump; a change that alters interpretation is a major
  bump.
- Host tests must cover: unknown incompat bit refuses; unknown ro_compat allows
  read-only; unknown compat is ignored; newer major refuses; torn checksum refuses;
  nonzero reserved byte refuses.
- Tooling: the format doc and the checker both need to know the bit assignments, so the
  header and the doc must change in the same commit.

## 5. Open questions for the Director

1. Is a 16-bit major/minor split enough, or should `version` be widened to 32 bits at
   this revision (still a version 2 layout change)?
2. Should the version 1 → 2 migration tool ship in this wave at all, or is "version 1
   is discarded" acceptable given no installed base?
3. Where should the checksum algorithm id live: a new superblock field, or encoded in
   the feature word?
4. Does a read-only mount of a newer-minor volume have to be supported, or is refusing
   everything newer simpler and therefore better until read-only support exists?
