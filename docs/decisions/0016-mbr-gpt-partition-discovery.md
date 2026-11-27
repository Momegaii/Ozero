# ADR 0016: MBR and GPT partition table discovery

- **Status:** Accepted
- **Date:** 2026-11-26
- **Wave:** 4.4 (partitions)
- **Related:** Wave 4 storage directive (`docs/wave4.md`), `include/ozero/partition.h`,
  `kernel/src/partition.c`, `tests/partition_test.c`

## 1. Context

Stage 4 storage needs to discover partitions on a block device before a
filesystem can mount a volume. The block layer (Wave 4.0/4.1) owns device
registration and synchronous single-segment I/O; this module owns table
parsing, header/CRC validation, overlap detection and out-of-range LBA
rejection.

Two on-disk formats are in common use and must both be supported:

- **MBR** — the legacy IBM PC partition table in the first 512-byte sector,
  with up to 4 primary entries and a `0xAA55` signature at bytes 510-511.
- **GPT** — the GUID Partition Table, headed by a protective MBR at LBA 0 and
  the GPT header at LBA 1, with up to 128 entries of 128 bytes each starting
  at LBA 2, protected by a header CRC32 and an entry-array CRC32.

Ozero defines its own execution architecture and must not default to Linux
conventions. The ELF kernel image and Limine boot protocol are build/boot
implementation details; neither constrains the on-disk partition format, and
no ELF, Linux syscall, glibc or Linux userspace assumption is made here.

## 2. Alternatives

| Alternative | Trade-off |
|---|---|
| **MBR + GPT, both validated (chosen)** | Covers bootable media from both eras. MBR is simple and ubiquitous; GPT handles disks >2 TiB and carries unique partition GUIDs and names. Both are validated independently: CRCs, overlap, out-of-range. |
| GPT only | Simpler implementation, but rejects every MBR-partitioned disk — including most boot media and all existing test images. Not acceptable for a general-purpose block layer. |
| MBR only | Rejects GPT disks, which are the modern standard for large and UEFI-booted media. Not acceptable. |
| Defer to a userspace partition tool | Moves parsing out of the kernel, but the kernel needs to know partition extents before mounting a filesystem on a partition. Userspace tooling is Stage 5+. |

## 3. Decision

1. **LBA unit is 512 bytes.** All partition table LBAs (MBR CHS/LBA fields, GPT
   header LBAs, GPT entry first/last LBA) are interpreted as 512-byte sector
   units. The block layer reports `geo.nr_sectors` in logical-block units and
   `geo.logical_block_size` in bytes, so discovery converts:
   `nr_sectors_512 = nr_sectors * (logical_block_size / 512)`.

2. **Protective MBR defers to GPT.** LBA 0 is read first. If the `0xAA55`
   signature is absent, return `O0_ENOENT`. If present, parse any real MBR
   entries. If a type-`0xEE` protective entry covers the disk, attempt GPT
   first (header at LBA 1); fall back to plain MBR only if GPT validation
   fails.

3. **GPT validation order.** Header signature, revision, header size and entry
   size are checked first. Entry-start LBA is range-checked against the
   device. The header CRC32 is computed over `header_size` bytes with the CRC
   field zeroed; the entry-array CRC32 is computed over `num_entries *
   entry_size` bytes starting at `entry_start_lba`. Both must match before any
   entry is parsed.

4. **Entry validation.** Every non-empty entry is checked for: start LBA
   nonzero, nonzero extent, `start + size <= nr_sectors_512` (GPT) /
   `start < nr_sectors_512` (MBR, start may be 0 for a boot entry), and
   pairwise overlap against every other entry. Overlap returns `O0_EINVAL`;
   out-of-range returns `O0_ERANGE`.

5. **Output.** `o0_partition_discover()` fills `struct o0_partition_table` with
   `nr_entries` and `entries[]`, each carrying type GUID, unique GUID (GPT
   only), name (GPT only), start LBA, sector count, attributes (GPT only),
   MBR type byte and bootable flag (MBR only), plus a `gpt` discriminator.

6. **GUID formatting.** `o0_part_guid_to_string()` renders a GUID as lowercase
   `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`. It is a pure helper with no
   block-layer dependency.

## 4. Consequences

- The block layer is unchanged: partition discovery is a read-only client that
  calls `o0_block_read()` against the device's logical blocks.
- CRC32 (reflected, polynomial `0xEDB88320`) is implemented locally; no
  external library is introduced.
- The kernel image format (ELF64 via Limine) and boot protocol are
  build-time details; neither appears in the partition code. No Linux ABI,
  syscall or userspace convention is assumed.
- Windows compatibility (a first-class long-term goal) is unaffected: GPT is
  the shared on-disk format between Ozero and Windows, and MBR handling is
  read-only discovery, not a Windows runtime dependency.

## 5. Test plan

Host (`make test`, no QEMU):

1. MBR: valid two-entry table parses; missing signature returns `O0_ENOENT`;
   overlapping entries return `O0_EINVAL`; out-of-range extent returns
   `O0_ERANGE`.
2. GPT: valid table with one entry parses; bad header CRC returns `O0_EINVAL`;
   bad entry-array CRC returns `O0_EINVAL`; overlapping entries return
   `O0_EINVAL`; out-of-range extent returns `O0_ERANGE`.
3. GUID string formatting: known GUID, zero GUID, buffer-too-small failure.
4. Fuzz: flip every byte of a valid MBR image (512 flips) and every byte of a
   valid GPT image (4096 flips); the parser must return one of `O0_OK`,
   `O0_ENOENT`, `O0_EINVAL`, `O0_ERANGE` and must never crash.

The partition test is compiled with `-fsanitize=address,undefined` under
`make test-sanitize`.

Physical: never part of the gate. Partition discovery is read-only and does
not touch device registers; any physical run remains the Director's.

## 6. Open questions

1. **Backup GPT header.** The spec places a backup header at the last LBA and a
   duplicate entry array before it. Discovery currently reads only the primary.
   Fallback to backup is deferred; a corrupted primary currently fails rather
   than silently recovering.
2. **Extended MBR partitions.** Only the 4 primary MBR entries are parsed;
   EBR chained logical partitions are not yet supported. Deferred to a follow
   wave.
3. **Sub-512-byte logical blocks.** The conversion assumes
   `logical_block_size` is a multiple of 512 and divisible into it evenly.
   Devices with other block sizes return `O0_EINVAL` at discovery entry.
   None are known to exist in the QEMU device set.
