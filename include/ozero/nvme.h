/*
 * Ozero (o0) NVMe driver interface (Wave 4.3).
 *
 * The driver registers a real o0_block_dev behind the block layer. It sets up
 * one admin queue pair and one I/O queue pair in emulated NVM (register-level)
 * mode, maps BAR0 uncached, places completion interrupts in the device's MSI-X
 * table, and submits one command at a time. The submitting call polls the
 * completion phase bit with a bounded spin, so a lost interrupt is a timeout,
 * not a hang; the MSI-X handler counts and acknowledges nothing device-side.
 * A completion is accepted only when its command id matches as well as its
 * phase, and a command that times out or completes out of order disables and
 * re-enables the controller before anything reuses the queue, so a late
 * completion cannot be mistaken for the next command's or DMA into a released
 * buffer.
 *
 * Evidence limits: QEMU `-device nvme` only, one namespace, no PRP lists (one
 * page per request, which is what the block layer's synchronous path uses). No
 * physical controller is claimed.
 */
#ifndef O0_NVME_H
#define O0_NVME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Submission-queue entry command dword 0: opcode, fuse/PSDT zero, command id in
 * bits 31:16. Pure, for the host test. */
static inline uint32_t o0_nvme_cdw0(uint8_t opcode, uint16_t cid) {
    return (uint32_t)opcode | ((uint32_t)cid << 16);
}

/* Completion dword 3: command id in bits 15:0, phase tag in bit 16, status in
 * bits 31:17. */
static inline uint32_t o0_nvme_cqe_phase(uint32_t dw3) {
    return (dw3 >> 16) & 1u;
}

static inline uint32_t o0_nvme_cqe_status(uint32_t dw3) {
    return (dw3 >> 17) & 0x7FFFu;
}

static inline uint16_t o0_nvme_cqe_cid(uint32_t dw3) {
    return (uint16_t)(dw3 & 0xFFFFu);
}

/* A completion is ours only when both the phase tag and the command id match.
 * A phase match with a foreign id is a late completion from a command the driver
 * already gave up on, and consuming it would desynchronise the queue. */
static inline bool o0_nvme_cqe_matches(uint32_t dw3, uint32_t phase,
                                       uint16_t cid) {
    return o0_nvme_cqe_phase(dw3) == (phase & 1u) && o0_nvme_cqe_cid(dw3) == cid;
}

/* The driver programs PRP1 only, so a transfer must live inside the single page
 * that PRP1 points at. Anything longer needs a PRP list, which is not
 * implemented; submitting it anyway would let the controller DMA past the
 * buffer. */
static inline bool o0_nvme_prp_fits_one_page(uint64_t phys, uint32_t len) {
    if (len == 0u) {
        return false;
    }
    return (phys & 0xFFFu) + (uint64_t)len <= 0x1000u;
}

/* Probes PCI for an NVMe controller (class 01h/08h, prog-if 02h) and registers
 * namespace 1 under `name`. */
O0_NODISCARD o0_status o0_nvme_probe(const char *name,
                                     struct o0_block_dev **out);

/* Injected-error test: a two-block read starting on the last block of the
 * namespace is out of range. The controller must complete it with a non-zero
 * status and the driver must decode that as an error. */
O0_NODISCARD o0_status o0_nvme_inject_error(struct o0_block_dev *dev);

/* Interrupts taken by the driver since probe (diagnostics). */
uint64_t o0_nvme_irq_count(struct o0_block_dev *dev);

#ifdef __cplusplus
}
#endif

#endif /* O0_NVME_H */
