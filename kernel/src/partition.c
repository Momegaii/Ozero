/*
 * Ozero kernel - MBR and GPT partition table discovery (Wave 4.4).
 *
 * Parses the partition table on top of the block layer. Reads LBA 0 to decide
 * between MBR and GPT (a protective MBR defers to GPT), validates headers,
 * CRCs, overlapping entries and out-of-range LBAs against the device geometry.
 *
 * See docs/decisions/0016-mbr-gpt-partition-discovery.md.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ozero/block.h"
#include "ozero/partition.h"
#include "ozero/status.h"
#include "ozero/types.h"

/* ------------------------------------------------------------------ */
/* CRC32 (reflected, polynomial 0xEDB88320)                           */
/* ------------------------------------------------------------------ */

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
    }
    return ~crc;
}

/* ------------------------------------------------------------------ */
/* Sector reads                                                       */
/* ------------------------------------------------------------------ */

/*
 * Read one 512-byte GPT sector. The block layer addresses logical blocks of
 * dev->geo.logical_block_size bytes, so a GPT LBA (512-byte units) is mapped
 * to the logical block that contains it and the sub-sector is extracted.
 */
static o0_status part_read_sector(const struct o0_block_dev *dev,
                                  uint64_t lba, void *buffer) {
    uint32_t block_size = dev->geo.logical_block_size;
    uint32_t sectors_per_block = block_size / 512u;
    uint64_t block_lba = lba / (uint64_t)sectors_per_block;
    uint32_t sector_in_block = (uint32_t)(lba % (uint64_t)sectors_per_block);

    uint8_t block_buf[O0_PAGE_SIZE];
    o0_status st = o0_block_read((struct o0_block_dev *)dev,
                                 block_lba, block_buf, block_size);
    if (st != O0_OK) {
        return st;
    }
    memcpy(buffer, block_buf + (size_t)sector_in_block * 512u, 512u);
    return O0_OK;
}

/*
 * Read `count` consecutive 512-byte GPT sectors starting at `lba` into
 * `buffer`. Each sector is fetched individually so the caller's buffer need
 * only be count * 512 bytes.
 */
static o0_status part_read_sectors(const struct o0_block_dev *dev,
                                   uint64_t lba, uint32_t count, void *buffer) {
    uint8_t *buf = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; ++i) {
        o0_status st = part_read_sector(dev, lba + (uint64_t)i, buf);
        if (st != O0_OK) {
            return st;
        }
        buf += 512u;
    }
    return O0_OK;
}

/* ------------------------------------------------------------------ */
/* Little-endian helpers                                              */
/* ------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

/* ------------------------------------------------------------------ */
/* MBR                                                                */
/* ------------------------------------------------------------------ */

static bool mbr_signature_valid(const uint8_t *sector) {
    return rd16(sector + O0_MBR_SIGNATURE_OFFSET) == O0_MBR_SIGNATURE;
}

static bool mbr_entry_empty(const uint8_t *entry) {
    for (size_t i = 0; i < O0_MBR_PART_ENTRY_SIZE; ++i) {
        if (entry[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool mbr_entry_is_protective(const uint8_t *entry,
                                    uint64_t nr_sectors_512) {
    /* Type 0xEE = EFI GPT protective. Start LBA 1, size covers the disk. */
    if (entry[O0_MBR_TYPE_OFFSET] != 0xEEu) {
        return false;
    }
    uint64_t start = rd32(entry + O0_MBR_LBA_OFFSET);
    uint64_t size = rd32(entry + O0_MBR_SIZE_OFFSET);
    return start == 1u && size > 0u && start + size >= nr_sectors_512;
}

static o0_status mbr_parse(const struct o0_block_dev *dev,
                           const uint8_t *sector,
                           struct o0_partition_table *out) {
    uint64_t nr_sectors_512 = dev->geo.nr_sectors *
                              (uint64_t)(dev->geo.logical_block_size / 512u);
    bool protective = false;

    for (uint32_t i = 0; i < O0_MBR_PART_COUNT; ++i) {
        const uint8_t *entry = sector + O0_MBR_PART_OFFSET +
                               (size_t)i * O0_MBR_PART_ENTRY_SIZE;
        if (mbr_entry_empty(entry)) {
            continue;
        }
        if (mbr_entry_is_protective(entry, nr_sectors_512)) {
            protective = true;
            continue;
        }

        struct o0_partition *p = &out->entries[out->nr_entries++];
        memset(p, 0, sizeof(*p));
        p->bootable = entry[O0_MBR_BOOT_OFFSET] == 0x80u;
        p->mbr_type = entry[O0_MBR_TYPE_OFFSET];
        p->start_lba = rd32(entry + O0_MBR_LBA_OFFSET);
        p->nr_sectors = rd32(entry + O0_MBR_SIZE_OFFSET);
        p->gpt = false;
    }

    out->gpt = false;
    return protective ? O0_EINVAL : O0_OK; /* caller decides GPT vs MBR */
}

/*
 * Validate MBR entries: no overlaps, all extents inside the device. Returns
 * O0_ERANGE on out-of-range, O0_EINVAL on overlap.
 */
static o0_status mbr_validate(const struct o0_partition_table *tbl,
                              uint64_t nr_sectors_512) {
    for (uint32_t i = 0; i < tbl->nr_entries; ++i) {
        const struct o0_partition *a = &tbl->entries[i];
        if (a->start_lba >= nr_sectors_512 || a->nr_sectors == 0u ||
            a->start_lba > nr_sectors_512 - a->nr_sectors) {
            return O0_ERANGE;
        }
        for (uint32_t j = i + 1; j < tbl->nr_entries; ++j) {
            const struct o0_partition *b = &tbl->entries[j];
            uint64_t a_end = a->start_lba + a->nr_sectors;
            uint64_t b_end = b->start_lba + b->nr_sectors;
            if (a->start_lba < b_end && b->start_lba < a_end) {
                return O0_EINVAL;
            }
        }
    }
    return O0_OK;
}

/* ------------------------------------------------------------------ */
/* GPT                                                                */
/* ------------------------------------------------------------------ */

struct O0_PACKED gpt_header {
    uint8_t signature[8];
    uint8_t revision[4];
    uint8_t header_size[4];
    uint8_t header_crc32[4];
    uint8_t reserved[4];
    uint8_t current_lba[8];
    uint8_t backup_lba[8];
    uint8_t first_usable_lba[8];
    uint8_t last_usable_lba[8];
    uint8_t disk_guid[16];
    uint8_t entry_start_lba[8];
    uint8_t num_entries[4];
    uint8_t entry_size[4];
    uint8_t entry_array_crc32[4];
};

struct O0_PACKED gpt_entry {
    uint8_t type_guid[16];
    uint8_t part_guid[16];
    uint8_t first_lba[8];
    uint8_t last_lba[8];
    uint8_t attributes[8];
    uint8_t name[72]; /* 36 UTF-16LE code units */
};

static bool guid_is_zero(const struct o0_part_guid *g) {
    for (size_t i = 0; i < 16; ++i) {
        if (g->b[i] != 0u) {
            return false;
        }
    }
    return true;
}

static o0_status gpt_parse(const struct o0_block_dev *dev,
                           struct o0_partition_table *out) {
    uint64_t nr_sectors_512 = dev->geo.nr_sectors *
                              (uint64_t)(dev->geo.logical_block_size / 512u);

    /* Read the primary GPT header at LBA 1. */
    uint8_t sector[512];
    o0_status st = part_read_sector(dev, O0_GPT_PRIMARY_HEADER_LBA, sector);
    if (st != O0_OK) {
        return st;
    }

    const struct gpt_header *hdr = (const struct gpt_header *)sector;

    if (rd64(hdr->signature) != O0_GPT_HEADER_SIGNATURE) {
        return O0_EINVAL;
    }
    if (rd32(hdr->revision) != O0_GPT_HEADER_REVISION) {
        return O0_EINVAL;
    }
    uint32_t header_size = rd32(hdr->header_size);
    if (header_size < O0_GPT_HEADER_SIZE_MIN || header_size > 512u) {
        return O0_EINVAL;
    }
    uint32_t entry_size = rd32(hdr->entry_size);
    if (entry_size < O0_GPT_ENTRY_SIZE || entry_size > O0_PAGE_SIZE) {
        return O0_EINVAL;
    }
    uint32_t num_entries = rd32(hdr->num_entries);
    if (num_entries == 0u || num_entries > O0_PART_MAX_ENTRIES) {
        return O0_EINVAL;
    }
    uint64_t entry_start_lba = rd64(hdr->entry_start_lba);
    if (entry_start_lba == 0u || entry_start_lba >= nr_sectors_512) {
        return O0_EINVAL;
    }

    /* Header CRC: zero the CRC field, compute over header_size bytes. */
    uint8_t hdr_copy[512];
    memcpy(hdr_copy, sector, header_size);
    memset(hdr_copy + offsetof(struct gpt_header, header_crc32), 0, 4);
    uint32_t crc = crc32_update(0u, hdr_copy, header_size);
    if (crc != rd32(hdr->header_crc32)) {
        return O0_EINVAL;
    }

    uint64_t first_usable = rd64(hdr->first_usable_lba);
    uint64_t last_usable = rd64(hdr->last_usable_lba);
    if (first_usable == 0u || last_usable >= nr_sectors_512 ||
        first_usable > last_usable) {
        return O0_EINVAL;
    }

    /* Read and validate the partition entry array CRC. */
    uint32_t entry_array_bytes = (uint32_t)num_entries * entry_size;
    uint8_t entry_buf[O0_PAGE_SIZE];
    uint32_t entry_crc = 0u;
    uint32_t remaining = entry_array_bytes;
    uint64_t cur_lba = entry_start_lba;

    while (remaining > 0) {
        uint32_t this_chunk = (uint32_t)O0_MIN(remaining, (uint64_t)sizeof(entry_buf));
        st = part_read_sectors(dev, cur_lba,
                               (uint32_t)((this_chunk + 511u) / 512u), entry_buf);
        if (st != O0_OK) {
            return st;
        }
        entry_crc = crc32_update(entry_crc, entry_buf, this_chunk);
        remaining -= this_chunk;
        cur_lba += (uint64_t)((this_chunk + 511u) / 512u);
    }
    if (entry_crc != rd32(hdr->entry_array_crc32)) {
        return O0_EINVAL;
    }

    /* Parse entries. */
    remaining = entry_array_bytes;
    cur_lba = entry_start_lba;
    while (remaining > 0 && out->nr_entries < O0_PART_MAX_ENTRIES) {
        uint32_t this_chunk = (uint32_t)O0_MIN(remaining, (uint64_t)sizeof(entry_buf));
        st = part_read_sectors(dev, cur_lba,
                               (uint32_t)((this_chunk + 511u) / 512u), entry_buf);
        if (st != O0_OK) {
            return st;
        }
        uint32_t offset = 0;
        while (offset + entry_size <= this_chunk &&
               out->nr_entries < O0_PART_MAX_ENTRIES) {
            const struct gpt_entry *entry =
                (const struct gpt_entry *)(entry_buf + offset);
            if (!guid_is_zero((const struct o0_part_guid *)entry->type_guid)) {
                struct o0_partition *p = &out->entries[out->nr_entries++];
                memset(p, 0, sizeof(*p));
                memcpy(p->type.b, entry->type_guid, 16);
                memcpy(p->guid.b, entry->part_guid, 16);
                p->start_lba = rd64(entry->first_lba);
                uint64_t last_lba = rd64(entry->last_lba);
                p->nr_sectors = last_lba >= p->start_lba
                                    ? last_lba - p->start_lba + 1u
                                    : 0u;
                p->attributes = rd64(entry->attributes);
                /* Decode the UTF-16LE name (36 code units). */
                for (uint32_t k = 0; k < 36u && offset + 56 + k * 2 < this_chunk; ++k) {
                    uint16_t ch = (uint16_t)(entry->name[k * 2] |
                                             ((uint16_t)entry->name[k * 2 + 1] << 8));
                    if (ch == 0u) {
                        break;
                    }
                    if (ch < 0x80u) {
                        p->name[k] = (char)ch;
                    } else {
                        p->name[k] = '?';
                    }
                }
                p->name[O0_PART_NAME_MAX - 1] = '\0';
                p->gpt = true;
            }
            offset += entry_size;
        }
        remaining -= this_chunk;
        cur_lba += (uint64_t)((this_chunk + 511u) / 512u);
    }

    out->gpt = true;
    return O0_OK;
}

static o0_status gpt_validate(const struct o0_partition_table *tbl,
                              uint64_t nr_sectors_512) {
    for (uint32_t i = 0; i < tbl->nr_entries; ++i) {
        const struct o0_partition *a = &tbl->entries[i];
        if (a->start_lba == 0u || a->nr_sectors == 0u ||
            a->start_lba > nr_sectors_512 - a->nr_sectors) {
            return O0_ERANGE;
        }
        for (uint32_t j = i + 1; j < tbl->nr_entries; ++j) {
            const struct o0_partition *b = &tbl->entries[j];
            uint64_t a_end = a->start_lba + a->nr_sectors;
            uint64_t b_end = b->start_lba + b->nr_sectors;
            if (a->start_lba < b_end && b->start_lba < a_end) {
                return O0_EINVAL;
            }
        }
    }
    return O0_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

o0_status o0_partition_discover(const struct o0_block_dev *dev,
                                struct o0_partition_table *out) {
    if (dev == NULL || out == NULL) {
        return O0_EINVAL;
    }
    if (dev->geo.logical_block_size == 0u ||
        dev->geo.logical_block_size > O0_PAGE_SIZE) {
        return O0_EINVAL;
    }
    if (dev->geo.nr_sectors == 0u) {
        return O0_EINVAL;
    }

    uint64_t nr_sectors_512 = dev->geo.nr_sectors *
                              (uint64_t)(dev->geo.logical_block_size / 512u);

    memset(out, 0, sizeof(*out));

    /* Read LBA 0 (the MBR / protective MBR sector). */
    uint8_t sector[512];
    o0_status st = part_read_sector(dev, 0u, sector);
    if (st != O0_OK) {
        return st;
    }

    if (!mbr_signature_valid(sector)) {
        return O0_ENOENT;
    }

    /* Try GPT first: a protective MBR means LBA 1 holds the GPT header. */
    st = gpt_parse(dev, out);
    if (st == O0_OK) {
        return gpt_validate(out, nr_sectors_512);
    }

    /* Fall back to plain MBR. */
    struct o0_partition_table mbr;
    memset(&mbr, 0, sizeof(mbr));
    st = mbr_parse(dev, sector, &mbr);
    if (st != O0_OK) {
        return st; /* protective MBR present but GPT failed */
    }
    st = mbr_validate(&mbr, nr_sectors_512);
    if (st != O0_OK) {
        return st;
    }
    *out = mbr;
    return O0_OK;
}

long o0_part_guid_to_string(const struct o0_part_guid *guid,
                            char *out, size_t size) {
    if (guid == NULL || out == NULL || size == 0u) {
        return -1;
    }
    static const char hex[] = "0123456789abcdef";
    const uint8_t *b = guid->b;
    int groups[5] = {4, 2, 2, 2, 6};
    int group_ofs[5] = {0, 4, 6, 8, 10};
    size_t pos = 0;
    for (int g = 0; g < 5; ++g) {
        for (int i = 0; i < groups[g]; ++i) {
            if (pos + 1 >= size) {
                return -1;
            }
            int bi = group_ofs[g] + i;
            out[pos++] = hex[(b[bi] >> 4) & 0xFu];
            out[pos++] = hex[b[bi] & 0xFu];
        }
        if (g < 4) {
            if (pos + 1 >= size) {
                return -1;
            }
            out[pos++] = '-';
        }
    }
    if (pos + 1 >= size) {
        return -1;
    }
    out[pos] = '\0';
    return (long)(pos + 1);
}
