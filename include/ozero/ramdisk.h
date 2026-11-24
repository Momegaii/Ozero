/* Ozero kernel - in-memory block device (the Stage 4 reference driver).
 *
 * The RAM disk is a real o0_block_dev: it is registered with the block layer,
 * exposes sectors and geometry, and completes requests through the same queue
 * callbacks a hardware protocol driver will use. Its purpose is to make the
 * block layer and the filesystem testable before an ATA/AHCI/NVMe driver and
 * the PCI/DMA infrastructure exist.
 *
 * Limits: two devices, at most 4 MiB each, 4 KiB logical blocks, backing pages
 * taken from the PMM and addressed through the direct map. It is not a
 * persistent store and holds no data across a reboot.
 */
#ifndef O0_RAMDISK_H
#define O0_RAMDISK_H

#include <stddef.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_RAMDISK_MAX_DEVICES 2u
#define O0_RAMDISK_MAX_BYTES (4u * 1024u * 1024u)
#define O0_RAMDISK_MAX_PAGES (O0_RAMDISK_MAX_BYTES / O0_PAGE_SIZE)
#define O0_RAMDISK_BLOCK_SIZE 4096u

/* bytes is rounded down to a whole 4 KiB block and must be at least 64 KiB. */
O0_NODISCARD o0_status o0_ramdisk_create(const char *name, size_t bytes,
                                         struct o0_block_dev **out);

/* Unregisters the device and returns its backing pages to the PMM. The caller
 * must have unmounted any filesystem that uses it. */
o0_status o0_ramdisk_destroy(struct o0_block_dev *dev);

/* Test-only fault injection. Every transfer whose LBA range covers fail_lba
 * fails with status until count transfers have been failed (count < 0 means
 * unlimited). O0_BLOCK_OK disables injection. */
o0_status o0_ramdisk_inject_fault(struct o0_block_dev *dev,
                                  o0_block_status status,
                                  uint64_t fail_lba,
                                  int64_t count);

#ifdef __cplusplus
}
#endif

#endif /* O0_RAMDISK_H */
