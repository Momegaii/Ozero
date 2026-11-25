# ADR 0012: O0FS crash consistency — redo journal versus copy-on-write

- **Status:** Accepted-provisional (design only; nothing implemented; Director review requested)
- **Date:** 2026-11-25
- **Wave:** 4.0 (design), implemented in 4.5
- **Related:** ADR 0003 (Stage 4 storage substrate), ADR 0013 (format versioning),
  ADR 0014 (async completion and DMA ownership), `docs/o0fs-format.md`,
  `docs/wave4.md`

## 1. Context

The Stage 4 draft filesystem claims no crash consistency. `o0fs.c` attempts an
in-memory rollback when a write fails: it captures undo images of the data blocks it
overwrites and tries to restore data, allocation bitmaps and the previous inode. That
protects against a *reported* I/O error inside one operation. It does not survive a
power loss: nothing is ordered on the device, the superblock is rewritten by `sync`
independently of the metadata it describes, and there is no recovery pass on mount.

Wave 4.5 replaces that with a real mechanism. The choice has to fit what already
exists:

- fixed 4 KiB blocks, one volume per device, at most 4096 blocks (16 MiB);
- one block bitmap and one inode bitmap, each a single block;
- 128-byte inodes with 12 direct block pointers and one single-indirect block;
- free-block and free-inode counters stored in the superblock;
- a block layer that already exposes `FLUSH` and `FUA`, a single completion per bio,
  timeouts with quarantine, and driver-confirmed cancellation (ADR 0003, ADR 0014).

Per `AGENTS.md`, this ADR borrows techniques (journalling, copy-on-write) as references.
Neither Linux nor any other filesystem is treated as the design authority; the format,
recovery rules and metadata layout below are Ozero's own.

## 2. Alternatives

| Alternative | How it works | Trade-off |
|---|---|---|
| **Redo journal, metadata only, ordered data (chosen)** | Metadata changes are written as checksummed transaction records to a journal region, flushed, then committed by a single sequence-numbered commit block. Data blocks are written and flushed *before* the commit that references them. Recovery replays committed transactions and discards uncommitted ones. | Bounded, simple recovery; one extra write per transaction and a journal region; needs checksums, sequence numbers and a recovery path. Fits the existing inode and bitmap layout without changing addressing. |
| **Copy-on-write metadata tree** | Never overwrite live metadata; write new blocks, then switch an atomic superblock root pointer. | No replay and no journal space; but requires an indirection layer (B-tree or similar) to find the current metadata, a free-space reuse policy with safe epochs, and garbage collection. It would replace the fixed inode table and bitmap design, which is a much larger change than Wave 4.5 should carry. |
| **Log-structured volume** | All writes append to segments; garbage collector reclaims. | Excellent sequential write behaviour, but high write amplification and a complex collector; poor fit for a 16 MiB volume with fixed bitmaps. |
| **Ordering plus fsck only (soft updates style)** | Careful write order, no journal; a checker repairs after a crash. | Cheapest on disk space, but the guarantee is "eventually consistent after a full scan" and every ordering rule must be proved by hand. Also needs a checker anyway, which we need regardless. |
| **No change (keep rollback only)** | Today's behaviour. | Does not survive power loss, which is the Stage 4 exit criterion being closed. Rejected. |

## 3. Decision (proposed)

1. **Redo journal, metadata only.** Data blocks are written in place; metadata blocks
   (superblock, both bitmaps, inode blocks, indirect blocks, directory blocks) are
   journalled.
2. **Ordered data writes.** A transaction that references new data must not commit
   before those data blocks have completed *and* been flushed. A transaction that frees
   or reuses a data block must commit before the block can be handed to another file
   (no stale-data exposure after replay).
3. **Atomic commit.** A transaction is valid only if its commit record is present with a
   correct checksum and a sequence number exactly one greater than the last committed
   one. Torn or partial records are discarded, never partially applied.
4. **Idempotent replay.** Replay writes whole blocks and is safe to repeat; recovery
   runs before the volume accepts any other operation, and mount refuses a volume whose
   journal cannot be resolved.
5. **Barriers are explicit.** Durability points are explicit `FLUSH` (and `FUA` where
   the device advertises it). The filesystem never assumes that two completed writes
   reached the medium in submission order; ADR 0014 defines what the block layer
   guarantees.
6. **Superblock is transactional too.** `free_blocks`/`free_inodes` are derived from the
   bitmaps during recovery rather than trusted from the last superblock write, so a torn
   superblock cannot mis-report allocation.
7. **A consistency checker ships with it.** It validates bitmaps against reachable
   inodes, directory entries against inode types, link counts, indirect blocks, and
   that no block is referenced twice. It runs in host tests after every injected crash
   and is available for recovery diagnostics.

Chosen over copy-on-write because it preserves the current on-disk structures and can be
implemented and crash-tested within Wave 4.5, while copy-on-write would force an
addressing redesign first. That redesign is not rejected forever; it is recorded as a
future option once a VFS and larger volumes exist.

## 4. Consequences

- The on-disk format gains a journal region and journal metadata: a format change, so
  ADR 0013's versioning rules apply (this is version 2).
- Every metadata-modifying operation becomes a transaction; the existing
  format/mount/create/read/write/truncate/unlink/mkdir/rmdir/list behaviour stays the
  same from the caller's side.
- Space accounting changes: the journal is reserved and not reported as free.
- Write throughput drops (journal write plus commit) and small-file writes pay an extra
  flush; acceptable for the emulator-first stage, and measured before any claim.
- Power-loss testing becomes mandatory: crash injection at every block write, flush and
  reorder point, then remount and run the checker.

## 5. Open questions for the Director

1. **Journal placement:** a fixed region described by the superblock (simple, testable)
   versus blocks allocated from the normal bitmap (flexible, but allocation must itself
   be journalled). The proposal is a fixed region sized at format time.
2. **Transaction granularity:** one transaction per operation (simplest) versus grouping
   independent operations (better throughput, more replay complexity).
3. **Data journalling for small files:** metadata-only is the proposal; should files
   below one block be data-journalled to make their content atomic as well?
4. **Checksum:** keep 32-bit FNV-1a for continuity, or adopt CRC32C for stronger torn-write
   detection? FNV-1a is not a CRC and has weaker burst-error properties.
5. **`FUA` trust:** is `FUA` relied on where advertised, or is an explicit `FLUSH` always
   issued (slower, simpler to reason about, and independent of device quirks)?
6. **Recovery policy on unresolvable journal:** refuse the mount outright (proposal) or
   mount read-only for inspection?
