/* Ozero kernel - ACPI RSDP/RSDT/XSDT/MADT parser.
 *
 * All multi-byte fields are little-endian by ACPI definition. Checksums cover
 * the table length declared in the header, so a truncated or overlong table is
 * rejected before any record is trusted.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ozero/acpi.h"
#include "ozero/status.h"

#define O0_RSDP_SIG "RSD PTR "
#define O0_RSDT_SIG "RSDT"
#define O0_XSDT_SIG "XSDT"
#define O0_MADT_SIG "APIC"
#define O0_ACPI_HEADER_BYTES 36u
#define O0_MADT_HEADER_BYTES 44u

static struct o0_acpi_info last_info;
static bool last_info_valid;

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static bool signature_is(const uint8_t *p, const char *sig) {
    return memcmp(p, sig, 4) == 0;
}

static bool checksum_ok(const uint8_t *p, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum == 0;
}

static const uint8_t *phys_to_virt(uint64_t phys, uint64_t hhdm_offset) {
    if (phys == 0 || phys > UINT64_MAX - hhdm_offset) {
        return NULL;
    }
    return (const uint8_t *)(uintptr_t)(phys + hhdm_offset);
}

static o0_status read_table(uint64_t phys, uint64_t hhdm_offset,
                            const char *signature, const uint8_t **out,
                            uint32_t *out_len) {
    const uint8_t *table = phys_to_virt(phys, hhdm_offset);
    if (table == NULL) {
        return O0_ERANGE;
    }
    if (!signature_is(table, signature)) {
        return O0_EINVAL;
    }
    uint32_t length = rd32(table + 4);
    if (length < O0_ACPI_HEADER_BYTES || length > O0_ACPI_MAX_TABLE_BYTES) {
        return O0_ERANGE;
    }
    if (!checksum_ok(table, length)) {
        return O0_EINVAL;
    }
    *out = table;
    *out_len = length;
    return O0_OK;
}

static bool cpu_seen(const struct o0_acpi_info *info, uint32_t apic_id) {
    for (uint32_t i = 0; i < info->cpu_count; ++i) {
        if (info->cpus[i].apic_id == apic_id) {
            return true;
        }
    }
    return false;
}

static o0_status parse_madt(const uint8_t *table, uint32_t length,
                            struct o0_acpi_info *out) {
    if (length < O0_MADT_HEADER_BYTES || !checksum_ok(table, length)) {
        return O0_EINVAL;
    }
    out->lapic_address = rd32(table + 36);
    out->madt_flags = rd32(table + 40);

    uint32_t offset = O0_MADT_HEADER_BYTES;
    while (offset + 2u <= length) {
        uint8_t type = table[offset];
        uint8_t record_len = table[offset + 1u];
        if (record_len < 2u || (uint32_t)record_len > length - offset) {
            return O0_ERANGE;
        }
        const uint8_t *rec = table + offset;

        if (type == 0u && record_len >= 8u) {
            if (out->cpu_count >= O0_ACPI_MAX_CPUS) {
                return O0_ENOMEM;
            }
            uint32_t apic_id = rec[3];
            if (!cpu_seen(out, apic_id)) {
                struct o0_acpi_cpu *cpu = &out->cpus[out->cpu_count++];
                cpu->acpi_uid = rec[2];
                cpu->apic_id = apic_id;
                cpu->flags = rd32(rec + 4);
                cpu->enabled = (cpu->flags & 1u) != 0;
            }
        } else if (type == 9u && record_len >= 16u) {
            if (out->cpu_count >= O0_ACPI_MAX_CPUS) {
                return O0_ENOMEM;
            }
            uint32_t apic_id = rd32(rec + 8);
            if (!cpu_seen(out, apic_id)) {
                struct o0_acpi_cpu *cpu = &out->cpus[out->cpu_count++];
                cpu->acpi_uid = rd32(rec + 4);
                cpu->apic_id = apic_id;
                cpu->flags = rd32(rec + 12);
                cpu->enabled = (cpu->flags & 1u) != 0;
            }
        } else if (type == 1u && record_len >= 12u) {
            if (out->ioapic_count >= O0_ACPI_MAX_IOAPICS) {
                return O0_ENOMEM;
            }
            struct o0_acpi_ioapic *io = &out->ioapics[out->ioapic_count++];
            io->id = rec[2];
            io->address = rd32(rec + 4);
            io->gsi_base = rd32(rec + 8);
        } else if (type == 2u && record_len >= 10u) {
            if (out->override_count >= O0_ACPI_MAX_OVERRIDES) {
                return O0_ENOMEM;
            }
            struct o0_acpi_override *ov = &out->overrides[out->override_count++];
            ov->bus = rec[2];
            ov->source = rec[3];
            ov->gsi = rd32(rec + 4);
            ov->flags = (uint16_t)(rec[8] | ((uint16_t)rec[9] << 8));
        }
        offset += record_len;
    }
    if (offset != length) {
        return O0_ERANGE;
    }
    if (out->cpu_count == 0) {
        return O0_ENOENT;
    }
    return O0_OK;
}

o0_status o0_acpi_parse(const void *rsdp_ptr, uint64_t hhdm_offset,
                        uint32_t bsp_apic_id, struct o0_acpi_info *out) {
    if (rsdp_ptr == NULL || out == NULL) {
        return O0_EINVAL;
    }
    const uint8_t *rsdp = (const uint8_t *)rsdp_ptr;
    if (!signature_is(rsdp, O0_RSDP_SIG)) {
        return O0_EINVAL;
    }
    uint8_t revision = rsdp[15];
    size_t rsdp_len = revision >= 2u ? 36u : 20u;
    if (!checksum_ok(rsdp, 20u)) {
        return O0_EINVAL;
    }
    if (revision >= 2u && !checksum_ok(rsdp, rsdp_len)) {
        return O0_EINVAL;
    }

    uint64_t table_phys = 0;
    bool xsdt = false;
    const char *table_sig = O0_RSDT_SIG;
    if (revision >= 2u) {
        table_phys = rd64(rsdp + 24);
        table_sig = O0_XSDT_SIG;
        xsdt = table_phys != 0;
    }
    if (table_phys == 0) {
        table_phys = rd32(rsdp + 16);
        table_sig = O0_RSDT_SIG;
        xsdt = false;
    }
    if (table_phys == 0) {
        return O0_ENOENT;
    }

    const uint8_t *sdt = NULL;
    uint32_t sdt_len = 0;
    o0_status st = read_table(table_phys, hhdm_offset, table_sig, &sdt, &sdt_len);
    if (st != O0_OK) {
        return st;
    }

    uint32_t entry_size = xsdt ? 8u : 4u;
    uint32_t entry_bytes = sdt_len - O0_ACPI_HEADER_BYTES;
    if (entry_bytes % entry_size != 0) {
        return O0_ERANGE;
    }

    for (uint32_t off = O0_ACPI_HEADER_BYTES; off < sdt_len; off += entry_size) {
        uint64_t entry = xsdt ? rd64(sdt + off) : rd32(sdt + off);
        if (entry == 0) {
            continue;
        }
        const uint8_t *candidate = phys_to_virt(entry, hhdm_offset);
        if (candidate == NULL) {
            return O0_ERANGE;
        }
        if (!signature_is(candidate, O0_MADT_SIG)) {
            continue;
        }
        uint32_t madt_len = rd32(candidate + 4);
        if (madt_len < O0_MADT_HEADER_BYTES || madt_len > O0_ACPI_MAX_TABLE_BYTES) {
            return O0_ERANGE;
        }
        memset(out, 0, sizeof(*out));
        st = parse_madt(candidate, madt_len, out);
        if (st != O0_OK) {
            return st;
        }
        out->bsp_apic_id = bsp_apic_id;
        bool bsp_found = false;
        if (bsp_apic_id != UINT32_MAX) {
            for (uint32_t i = 0; i < out->cpu_count; ++i) {
                if (out->cpus[i].apic_id == bsp_apic_id) {
                    bsp_found = true;
                    break;
                }
            }
            if (!bsp_found) {
                return O0_ENOENT;
            }
        }
        if (bsp_apic_id == UINT32_MAX) {
            out->bsp_apic_id = out->cpus[0].apic_id;
        }
        return O0_OK;
    }
    return O0_ENOENT;
}

void o0_acpi_set_info(const struct o0_acpi_info *info) {
    if (info == NULL) {
        last_info_valid = false;
        return;
    }
    last_info = *info;
    last_info_valid = true;
}

const struct o0_acpi_info *o0_acpi_info(void) {
    return last_info_valid ? &last_info : NULL;
}
