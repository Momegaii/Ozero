/*
 * Ozero (o0) AHCI (SATA) driver interface (Wave 4.3).
 *
 * The driver registers a real o0_block_dev, so the block layer's validation,
 * timeout, quarantine and cancellation rules apply unchanged. It programs one
 * AHCI port: command list, received-FIS area, one command table with a PRDT,
 * and the Identify Device data. Completion is interrupt-driven where an MSI
 * capability exists; the submitting call also polls PxCI with a bounded spin so
 * a missing or misrouted interrupt is a timeout, never a hang.
 *
 * Evidence limits: QEMU `ich9-ahci` only. No physical controller is claimed and
 * no NCQ or queue depth beyond one is used.
 */
#ifndef O0_AHCI_H
#define O0_AHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SATA register host-to-device FIS, 20 bytes (CFL = 5 dwords). Pure, so the host
 * test can check the field layout against the SATA specification without a
 * device. `count` is a sector count; 0 means 65536 on the wire. */
static inline void o0_ahci_encode_h2d(uint8_t fis[20], uint8_t command,
                                      uint64_t lba, uint16_t count) {
    for (int i = 0; i < 20; ++i) {
        fis[i] = 0;
    }
    fis[0] = 0x27; /* FIS type: register host-to-device */
    fis[1] = 0x80; /* C: update the command register */
    fis[2] = command;
    fis[3] = 0; /* features low */
    fis[4] = (uint8_t)(lba & 0xFF);
    fis[5] = (uint8_t)((lba >> 8) & 0xFF);
    fis[6] = (uint8_t)((lba >> 16) & 0xFF);
    fis[7] = 0x40; /* device: LBA mode */
    fis[8] = (uint8_t)((lba >> 24) & 0xFF);
    fis[9] = (uint8_t)((lba >> 32) & 0xFF);
    fis[10] = 0; /* features high */
    fis[11] = 0;
    fis[12] = (uint8_t)(count & 0xFF);
    fis[13] = (uint8_t)((count >> 8) & 0xFF);
    fis[14] = 0;
    fis[15] = 0;
}

/* PRDT data byte count field: bytes minus one, 22 bits. */
static inline uint32_t o0_ahci_prdt_dbc(uint32_t bytes) {
    return (bytes - 1u) & 0x3FFFFFu;
}

/* Probes PCI for an AHCI controller (class 01h/06h, prog-if 01h) and registers
 * a device for the first attached SATA disk under `name`. */
O0_NODISCARD o0_status o0_ahci_probe(const char *name,
                                     struct o0_block_dev **out);

/* Injected-error test: a DMA read that starts at the last sector but spans two
 * sectors runs past the end of the medium. The device must report the error
 * and the driver must decode it rather than hang or report success. */
O0_NODISCARD o0_status o0_ahci_inject_error(struct o0_block_dev *dev);

/* Interrupts taken by the driver since probe (diagnostics). */
uint64_t o0_ahci_irq_count(struct o0_block_dev *dev);

#ifdef __cplusplus
}
#endif

#endif /* O0_AHCI_H */
