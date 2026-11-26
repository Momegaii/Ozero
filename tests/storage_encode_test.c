/* Ozero kernel - host tests for the AHCI and NVMe command encoders.
 *
 * These are the pure parts of the Wave 4.3 drivers: the SATA host-to-device
 * register FIS and PRDT byte count, and the NVMe command/completion dwords. The
 * field positions come from the SATA AHCI specification and the NVMe base
 * specification; nothing here touches a device.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ozero/ahci.h"
#include "ozero/nvme.h"

static void test_ahci_fis_lba48(void) {
    uint8_t fis[20];
    memset(fis, 0xAA, sizeof(fis));
    o0_ahci_encode_h2d(fis, 0x25u, 0x123456789ABull, 0x0102u);

    assert(fis[0] == 0x27); /* register host-to-device FIS */
    assert(fis[1] == 0x80); /* C bit: command register is valid */
    assert(fis[2] == 0x25); /* READ DMA EXT */
    assert(fis[4] == 0xAB); /* LBA 7:0 */
    assert(fis[5] == 0x89); /* LBA 15:8 */
    assert(fis[6] == 0x67); /* LBA 23:16 */
    assert(fis[7] == 0x40); /* LBA mode */
    assert(fis[8] == 0x45); /* LBA 31:24 */
    assert(fis[9] == 0x23); /* LBA 39:32 */
    assert(fis[12] == 0x02); /* count 7:0 */
    assert(fis[13] == 0x01); /* count 15:8 */
    for (int i = 16; i < 20; ++i) {
        assert(fis[i] == 0);
    }
}

static void test_ahci_fis_identify(void) {
    uint8_t fis[20];
    o0_ahci_encode_h2d(fis, 0xECu, 0u, 0u);
    assert(fis[2] == 0xEC);
    assert(fis[4] == 0 && fis[5] == 0 && fis[6] == 0);
    assert(fis[8] == 0 && fis[9] == 0);
    assert(fis[12] == 0 && fis[13] == 0);
}

static void test_ahci_prdt_dbc(void) {
    assert(o0_ahci_prdt_dbc(512u) == 511u);
    assert(o0_ahci_prdt_dbc(4096u) == 4095u);
    assert(o0_ahci_prdt_dbc(1u) == 0u);
    /* Field is 22 bits; larger counts must not bleed into the reserved bits. */
    assert(o0_ahci_prdt_dbc(0x400000u) == 0x3FFFFFu);
}

static void test_nvme_cdw0(void) {
    uint32_t dw0 = o0_nvme_cdw0(0x02u, 0x1234u);
    assert((dw0 & 0xFFu) == 0x02u);
    assert(((dw0 >> 8) & 0xFFu) == 0u); /* fuse and PSDT stay zero */
    assert((dw0 >> 16) == 0x1234u);
}

static void test_nvme_cqe(void) {
    /* command id 0x0B, phase 1, status 0 (success) */
    uint32_t ok = 0x0Bu | (1u << 16);
    assert(o0_nvme_cqe_cid(ok) == 0x0Bu);
    assert(o0_nvme_cqe_phase(ok) == 1u);
    assert(o0_nvme_cqe_status(ok) == 0u);

    /* status 0x82 (LBA out of range) in bits 24:17, phase 0 */
    uint32_t error = 0x7u | (0x82u << 17);
    assert(o0_nvme_cqe_cid(error) == 0x7u);
    assert(o0_nvme_cqe_phase(error) == 0u);
    assert(o0_nvme_cqe_status(error) == 0x82u);
}

int main(void) {
    test_ahci_fis_lba48();
    test_ahci_fis_identify();
    test_ahci_prdt_dbc();
    test_nvme_cdw0();
    test_nvme_cqe();
    printf("storage encode tests: ok\n");
    return 0;
}
