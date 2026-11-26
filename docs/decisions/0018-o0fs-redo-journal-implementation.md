# ADR 0018: O0FS redo journal implementation (Wave 4.5)

- **Status:** Accepted
- **Date:** 2026-11-26
- **Wave:** 4.5 (crash-consistent O0FS)
- **Related:** ADR 0012 (crash consistency: redo journal chosen), ADR 0013 (format versioning),
  `kernel/src/o0fs.c`, `include/ozero/fs.h`, `tests/o0fs_journal_test.c`,
  `tests/fs_test.c`, `docs/o0fs-format.md`

## 1. Context

ADR 0012 chose a redo journal for O0FS crash consistency. The design goal:
after a power loss at any write point, remount must either replay committed
transactions to a consistent state or leave the volume exactly as it was at
the last committed transaction — never partially applied, never corrupt.

Ozero defines its own native execution architecture and does not default to
Linux conventions. No ELF, ELF64, Linux syscall, glibc or Linux userspace
assumption appears in this code. The journal format, recovery rules and
metadata layout below are Ozero's own, borrowing only techniques (redo
journalling, ordered data) as references.

## 2. Alternatives

| Alternative | How it works | Trade-off |
|---|---|---|
| **Metadata-only redo journal with ordered data (implemented)** | Metadata blocks (bitmaps, inode table, directory blocks, indirect blocks, superblock) are staged per-transaction, checksummed, written to a reserved journal region, flushed, then committed by a single sequence-numbered header block. Data blocks are written in place and flushed before the commit. Recovery replays committed transactions and discards uncommitted ones. | Simple, bounded recovery. One journal region plus one extra write per transaction. Preserves the existing inode/bitmap addressing. |
| **Metadata + data journal** | Data blocks are also journaled before being written in place. | Stronger atomicity for small files, but doubles journal space and write cost; deferred as an open question from ADR 0012 §5.3. |
| **Copy-on-write metadata tree** | Never overwrite live metadata; switch an atomic superblock root pointer. | Rejected by ADR 0012 — requires an addressing redesign (B-tree) too large for Wave 4.5. |

## 3. Decision

### 3.1 On-disk format changes (version 2)

A reserved journal region is inserted between the inode bitmap and the first
data block. The superblock records `journal_block`, `journal_blocks` and
`version = 2`. The journal holds one header block followed by record blocks.
Each block image spans two records: bytes `[0,4092)` in record `2i`, bytes
`[4092,4096)` in record `2i+1`, both carrying the same target block number.

### 3.2 Transaction lifecycle

1. `fs_txn_begin`: saves live block/inode bitmaps (for abort rollback), sets
   `active = true`, assigns `sequence = journal_sequence + 1`.
2. Metadata blocks are staged via `fs_meta_write` into transaction slots.
   Data blocks are written directly to disk and flushed first (ordered data).
3. `fs_txn_commit`: writes data records to the journal, flushes, writes the
   commit header (magic + sequence + record_count + FNV-1a checksum), flushes,
   then applies staged images to live locations and flushes them.
4. On abort: `fs_txn_abort` restores the saved bitmaps; any staged allocations
   are discarded, so uncommitted blocks/inodes stay free on disk.

### 3.3 Recovery

`fs_journal_recover` runs during mount before any other operation. It scans
the journal region for valid headers (magic, checksum, record count within
bounds, sequence greater than the last applied). For each valid transaction in
sequence order, it applies its record blocks to live locations and advances
`journal_sequence`. Uncommitted or torn transactions are skipped. Mount
refuses a volume whose journal cannot be resolved.

The header is copied into a stack struct before the checksum loop overwrites
the shared scratch page; each record's data payload is likewise copied before
the next record read overwrites `buf`. Record replay reconstructs each block
from `rec0->data` (bytes `[0,4092)`) plus `rec1->data` (bytes `[4092,4096)`),
never copying the `target_block` field as block data.

## 4. Consequences

- Every metadata-modifying operation (create, mkdir, write, truncate, unlink,
  rmdir) is a transaction. Caller-visible behavior is unchanged.
- `o0fs_check` (consistency checker) validates bitmaps against reachable
  inodes, directory entries against inode types, double-referenced blocks,
  indirect blocks and root-inode validity. It runs in host tests after every
  injected crash.
- Write throughput drops modestly (journal write + commit flush per
  transaction); acceptable for the emulator-first stage.
- Journal region is reserved at format time and not reported as free space.
- The format change requires ADR 0013 versioning: version 2 volumes are
  refused by version 1 code and vice versa.

## 5. Test plan

Host (`make test`, `make test-sanitize`):

- `tests/o0fs_journal_test.c`: 5 crash-recovery tests covering normal
  write+remount, crash before commit header, crash at commit header, crash
  during live application, and a clean-volume consistency check.
- `tests/fs_test.c`: existing O0FS host tests updated for journal semantics,
  including remount after injected I/O faults and write-failure consistency.

CI/Director condition (from ADR 0012 §4): crash injection at *every* write
point — each block write, each flush, every reorder window — with remount and
a consistency-checker pass after each injected crash. The journal test covers
the write path; the fs_test remount test covers metadata persistence.

## 6. Open questions

- Data journalling for sub-block files (ADR 0012 §5.3).
- CRC32C versus FNV-1a checksum strength (ADR 0012 §5.4).
- Whether `FUA` is relied on where advertised, or explicit `FLUSH` is always
  issued (ADR 0012 §5.5).
- Recovery policy on unresolvable journal: refuse mount (implemented) versus
  mount read-only for inspection (ADR 0012 §5.6).
