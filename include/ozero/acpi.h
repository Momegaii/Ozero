/* Ozero kernel - ACPI table discovery used by the SMP foundation.
 *
 * This is a parser, not a platform policy layer: it validates the RSDP, walks
 * the RSDT or XSDT, and returns the MADT records the kernel needs to describe
 * processors and interrupt controllers. Table addresses inside ACPI are
 * physical; the caller supplies the direct-map offset used to reach them.
 */
#ifndef O0_ACPI_H
#define O0_ACPI_H

#include <stdbool.h>
#include <stdint.h>

#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_ACPI_MAX_CPUS 64u
#define O0_ACPI_MAX_IOAPICS 8u
#define O0_ACPI_MAX_OVERRIDES 32u
#define O0_ACPI_MAX_TABLE_BYTES 0x10000u

struct o0_acpi_cpu {
    uint32_t acpi_uid;
    uint32_t apic_id;
    uint32_t flags;
    bool enabled;
    uint8_t reserved[3];
};

struct o0_acpi_ioapic {
    uint32_t id;
    uint32_t address;
    uint32_t gsi_base;
};

struct o0_acpi_override {
    uint8_t bus;
    uint8_t source;
    uint16_t flags;
    uint32_t gsi;
};

struct o0_acpi_info {
    uint32_t lapic_address;
    uint32_t madt_flags;
    uint32_t bsp_apic_id;
    uint32_t cpu_count;
    uint32_t ioapic_count;
    uint32_t override_count;
    struct o0_acpi_cpu cpus[O0_ACPI_MAX_CPUS];
    struct o0_acpi_ioapic ioapics[O0_ACPI_MAX_IOAPICS];
    struct o0_acpi_override overrides[O0_ACPI_MAX_OVERRIDES];
};

/* Parses the RSDP at `rsdp` (a virtual address) and the MADT it points at.
 * `hhdm_offset` maps the physical table addresses inside the RSDP/RSDT/XSDT.
 * `bsp_apic_id` may be UINT32_MAX when the caller does not know it; the first
 * enabled local APIC is then recorded as the BSP. Returns O0_ENOENT when no
 * MADT is present, O0_EINVAL for a malformed or checksum-failing table, and
 * O0_ENOMEM when the fixed record limits are exceeded. */
O0_NODISCARD o0_status o0_acpi_parse(const void *rsdp, uint64_t hhdm_offset,
                                     uint32_t bsp_apic_id,
                                     struct o0_acpi_info *out);

/* Stores the most recently parsed MADT for later SMP bring-up. */
void o0_acpi_set_info(const struct o0_acpi_info *info);
const struct o0_acpi_info *o0_acpi_info(void);

#ifdef __cplusplus
}
#endif

#endif /* O0_ACPI_H */
