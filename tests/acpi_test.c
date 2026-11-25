/* Host tests for the ACPI RSDP/XSDT/MADT parser. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/acpi.h"
#include "ozero/status.h"

#define BUF_SIZE 0x1000u
#define RSDP_OFF 0x000u
#define XSDT_OFF 0x100u
#define MADT_OFF 0x200u
#define HHDM ((uint64_t)(uintptr_t)buf)

static uint8_t buf[BUF_SIZE];

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static void fix_rsdp_checksums(void) {
    uint8_t *rsdp = buf + RSDP_OFF;
    rsdp[9] = 0;
    rsdp[32] = 0;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < 20u; ++i) {
        sum = (uint8_t)(sum + rsdp[i]);
    }
    rsdp[9] = (uint8_t)(0u - sum);
    sum = 0;
    for (uint32_t i = 0; i < 36u; ++i) {
        sum = (uint8_t)(sum + rsdp[i]);
    }
    rsdp[32] = (uint8_t)(0u - sum);
}

static uint32_t build_madt(uint32_t offset) {
    uint8_t *m = buf + offset;
    memset(m, 0, 0x100);
    memcpy(m, "APIC", 4);
    put32(m + 36, 0xfee00000u);
    put32(m + 40, 1u);
    uint32_t p = 44u;

    /* Local APIC: BSP + one enabled AP + one disabled AP. */
    m[p + 0] = 0;
    m[p + 1] = 8;
    m[p + 2] = 1;
    m[p + 3] = 0;
    put32(m + p + 4, 1u);
    p += 8;
    m[p + 0] = 0;
    m[p + 1] = 8;
    m[p + 2] = 2;
    m[p + 3] = 1;
    put32(m + p + 4, 1u);
    p += 8;
    m[p + 0] = 0;
    m[p + 1] = 8;
    m[p + 2] = 3;
    m[p + 3] = 2;
    put32(m + p + 4, 0u);
    p += 8;

    /* IOAPIC and one interrupt source override. */
    m[p + 0] = 1;
    m[p + 1] = 12;
    m[p + 2] = 9;
    put32(m + p + 4, 0xfec00000u);
    put32(m + p + 8, 0u);
    p += 12;
    m[p + 0] = 2;
    m[p + 1] = 10;
    m[p + 2] = 0;
    m[p + 3] = 2;
    put32(m + p + 4, 9u);
    m[p + 8] = 0x0d;
    m[p + 9] = 0;
    p += 10;

    /* x2APIC record for a fourth CPU. */
    m[p + 0] = 9;
    m[p + 1] = 16;
    put32(m + p + 4, 4);
    put32(m + p + 8, 3);
    put32(m + p + 12, 1);
    p += 16;

    put32(m + 4, p);
    m[9] = 0;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < p; ++i) {
        sum = (uint8_t)(sum + m[i]);
    }
    m[9] = (uint8_t)(0u - sum);
    return p;
}

static void build_tables(void) {
    memset(buf, 0, sizeof(buf));

    uint32_t madt_len = build_madt(MADT_OFF);
    uint8_t *xsdt = buf + XSDT_OFF;
    memcpy(xsdt, "XSDT", 4);
    put32(xsdt + 4, 36u + 8u);
    put64(xsdt + 36, MADT_OFF);
    xsdt[9] = 0;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < 44u; ++i) {
        sum = (uint8_t)(sum + xsdt[i]);
    }
    xsdt[9] = (uint8_t)(0u - sum);

    uint8_t *rsdp = buf + RSDP_OFF;
    memcpy(rsdp, "RSD PTR ", 8);
    rsdp[15] = 2;
    put32(rsdp + 16, 0); /* force XSDT */
    put32(rsdp + 20, 36);
    put64(rsdp + 24, XSDT_OFF);
    fix_rsdp_checksums();
    (void)madt_len;
}

static void test_accept(void) {
    build_tables();
    struct o0_acpi_info info;
    assert(o0_acpi_parse(buf + RSDP_OFF, HHDM, UINT32_MAX, &info) == O0_OK);
    assert(info.lapic_address == 0xfee00000u);
    assert(info.cpu_count == 4u);
    assert(info.cpus[0].apic_id == 0u && info.cpus[0].enabled);
    assert(info.cpus[1].apic_id == 1u && info.cpus[1].enabled);
    assert(info.cpus[2].apic_id == 2u && !info.cpus[2].enabled);
    assert(info.cpus[3].apic_id == 3u && info.cpus[3].enabled);
    assert(info.ioapic_count == 1u);
    assert(info.ioapics[0].address == 0xfec00000u);
    assert(info.override_count == 1u);
    assert(info.overrides[0].source == 2u && info.overrides[0].gsi == 9u);
    assert(info.bsp_apic_id == 0u);

    assert(o0_acpi_parse(buf + RSDP_OFF, HHDM, 1u, &info) == O0_OK);
    assert(info.bsp_apic_id == 1u);
    assert(o0_acpi_parse(buf + RSDP_OFF, HHDM, 99u, &info) == O0_ENOENT);
}

static void test_rejections(void) {
    struct o0_acpi_info info;
    build_tables();

    uint8_t *rsdp = buf + RSDP_OFF;
    rsdp[0] = 'X';
    assert(o0_acpi_parse(rsdp, HHDM, UINT32_MAX, &info) == O0_EINVAL);
    build_tables();

    rsdp[15] = 0;
    fix_rsdp_checksums();
    assert(o0_acpi_parse(rsdp, HHDM, UINT32_MAX, &info) == O0_ENOENT);
    build_tables();

    buf[XSDT_OFF + 9] ^= 0x01;
    assert(o0_acpi_parse(buf + RSDP_OFF, HHDM, UINT32_MAX, &info) == O0_EINVAL);
    build_tables();

    buf[MADT_OFF + 9] ^= 0x01;
    assert(o0_acpi_parse(buf + RSDP_OFF, HHDM, UINT32_MAX, &info) == O0_EINVAL);
    build_tables();

    put32(buf + XSDT_OFF + 4, 36u + 4u); /* not a whole 64-bit entry */
    buf[XSDT_OFF + 9] = 0;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < 40u; ++i) {
        sum = (uint8_t)(sum + buf[XSDT_OFF + i]);
    }
    buf[XSDT_OFF + 9] = (uint8_t)(0u - sum);
    assert(o0_acpi_parse(buf + RSDP_OFF, HHDM, UINT32_MAX, &info) == O0_ERANGE);
    build_tables();

    assert(o0_acpi_parse(NULL, HHDM, UINT32_MAX, &info) == O0_EINVAL);
    assert(o0_acpi_parse(buf + RSDP_OFF, HHDM, UINT32_MAX, NULL) == O0_EINVAL);
}

static void test_no_madt(void) {
    build_tables();
    memcpy(buf + MADT_OFF, "FACP", 4);
    assert(o0_acpi_parse(buf + RSDP_OFF, HHDM, UINT32_MAX, NULL) == O0_EINVAL);
    struct o0_acpi_info info;
    assert(o0_acpi_parse(buf + RSDP_OFF, HHDM, UINT32_MAX, &info) == O0_ENOENT);
}

int main(void) {
    test_accept();
    test_rejections();
    test_no_madt();
    puts("ACPI MADT tests: PASS");
    return 0;
}
