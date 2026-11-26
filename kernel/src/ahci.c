/* Ozero kernel - AHCI (SATA) driver (Wave 4.3).
 *
 * One port, one command slot, one PRDT entry: the block layer submits one
 * request at a time and the queue depth is reported as 1, so a command list of
 * 32 slots holds exactly one live command. The command list, received-FIS area,
 * command table and Identify data come from the DMA allocator and stay pinned
 * for the device lifetime.
 *
 * Completion: commands complete by clearing their PxCI bit. A bounded spin waits
 * for that, and the task-file error bit or PxIS.TFES becomes
 * O0_BLOCK_ERR_IO; a spin that ends first becomes O0_BLOCK_ERR_TIMEOUT. The MSI
 * handler acknowledges PxIS and counts, but the poll is what completion depends
 * on, which is stated in the docs rather than implied to be interrupt-driven.
 *
 * Evidence limits: QEMU `ich9-ahci` only. No physical controller is claimed, and
 * NCQ is not used.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/ahci.h"
#include "ozero/block.h"
#include "ozero/irq.h"
#include "ozero/lapic.h"
#include "ozero/mm.h"
#include "ozero/pci.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "string.h"

#define AHCI_MAX_DEVICES 2u
#define AHCI_WINDOW_BASE 0xffffc00020000000ull
#define AHCI_WINDOW_STRIDE 0x02000000ull
#define AHCI_TIMEOUT_SPINS 20000000u
#define AHCI_LINK_SPINS 8000000u

#define AHCI_REG_CAP 0x00u
#define AHCI_REG_GHC 0x04u
#define AHCI_REG_IS 0x08u
#define AHCI_REG_PI 0x0Cu
#define AHCI_REG_VS 0x10u

#define AHCI_GHC_IE (1u << 1)
#define AHCI_GHC_AE (1u << 31)

#define AHCI_PORT_BASE 0x100u
#define AHCI_PORT_STRIDE 0x80u

#define AHCI_PXCLB 0x00u
#define AHCI_PXCLBU 0x04u
#define AHCI_PXFB 0x08u
#define AHCI_PXFBU 0x0Cu
#define AHCI_PXIS 0x10u
#define AHCI_PXIE 0x14u
#define AHCI_PXCMD 0x18u
#define AHCI_PXTFD 0x20u
#define AHCI_PXSIG 0x24u
#define AHCI_PXSSTS 0x28u
#define AHCI_PXSTCL 0x2Cu
#define AHCI_PXSERR 0x30u
#define AHCI_PXSACT 0x34u
#define AHCI_PXCI 0x38u

#define AHCI_PXCMD_ST (1u << 0)
#define AHCI_PXCMD_FRE (1u << 4)
#define AHCI_PXCMD_FR (1u << 14)
#define AHCI_PXCMD_CR (1u << 15)

#define AHCI_PXIS_DHRS (1u << 0)
#define AHCI_PXIS_PSS (1u << 1)
#define AHCI_PXIS_DSS (1u << 2)
#define AHCI_PXIS_SDBS (1u << 3)
#define AHCI_PXIS_TFES (1u << 30)

#define AHCI_PXIE_ERRORS (AHCI_PXIS_DHRS | AHCI_PXIS_PSS | AHCI_PXIS_DSS | \
                          AHCI_PXIS_SDBS | AHCI_PXIS_TFES)

#define ATA_IDENTIFY 0xECu
#define ATA_READ_DMA_EXT 0x25u
#define ATA_WRITE_DMA_EXT 0x35u
#define ATA_FLUSH_CACHE_EXT 0xEAu

struct ahci_cmd_header {
    uint16_t flags;      /* bits 0-4 CFL in dwords, bit 5 W, bits 6-7 P */
    uint16_t prdtl;      /* PRDT entry count */
    volatile uint32_t prdbc; /* bytes transferred */
    uint32_t ctba;       /* command table physical address, low */
    uint32_t ctbau;      /* command table physical address, high */
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;        /* bytes minus one; bit 31 interrupt on completion */
} __attribute__((packed));

struct ahci_cmd_table {
    uint8_t cfis[64];
    uint8_t acmd[32];
    uint8_t reserved[16];
    struct ahci_prdt_entry prdt[8];
} __attribute__((packed));

struct o0_ahci {
    struct o0_block_dev dev;
    struct o0_block_queue queue;
    struct o0_pci_device pci;
    volatile uint8_t *abar;
    uint32_t port;
    uint64_t sectors;
    uint32_t sector_size;
    bool in_use;

    struct o0_dma_buffer cmd_list;
    struct o0_dma_buffer fis_area;
    struct o0_dma_buffer table;
    struct o0_dma_buffer identify;
    struct o0_dma_buffer io;
    struct ahci_cmd_header *hdr;
    struct ahci_cmd_table *ct;

    o0_vector_t vector;
    bool vector_ok;
    volatile uint64_t irq_count;
};

static struct o0_ahci ahci_devices[AHCI_MAX_DEVICES];

static inline uint32_t ahci_read(struct o0_ahci *ad, uint32_t off) {
    return *(volatile uint32_t *)(ad->abar + off);
}

static inline void ahci_write(struct o0_ahci *ad, uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(ad->abar + off) = value;
}

static inline uint32_t ahci_port_offset(uint32_t port, uint32_t off) {
    return AHCI_PORT_BASE + port * AHCI_PORT_STRIDE + off;
}

static inline uint32_t ahci_port_read(struct o0_ahci *ad, uint32_t port,
                                      uint32_t off) {
    return ahci_read(ad, ahci_port_offset(port, off));
}

static inline void ahci_port_write(struct o0_ahci *ad, uint32_t port,
                                   uint32_t off, uint32_t value) {
    ahci_write(ad, ahci_port_offset(port, off), value);
}

static bool ahci_port_wait(struct o0_ahci *ad, uint32_t port, uint32_t off,
                           uint32_t mask, uint32_t want, uint32_t spins) {
    for (uint32_t i = 0; i < spins; ++i) {
        if ((ahci_port_read(ad, port, off) & mask) == want) {
            return true;
        }
    }
    return false;
}

static void ahci_ignore(o0_status status) {
    (void)status;
}

static struct o0_ahci *ahci_from_dev(const struct o0_block_dev *dev) {
    for (size_t i = 0; i < AHCI_MAX_DEVICES; ++i) {
        if (&ahci_devices[i].dev == dev && ahci_devices[i].in_use) {
            return &ahci_devices[i];
        }
    }
    return NULL;
}

static bool ahci_irq(o0_gsi_t gsi, void *ctx) {
    (void)gsi;
    struct o0_ahci *ad = (struct o0_ahci *)ctx;
    if (ad == NULL) {
        return false;
    }
    /* Acknowledge every port that latched status; the value is required for the
     * write-one-to-clear. The global IS is cleared the same way. */
    uint32_t pi = ahci_read(ad, AHCI_REG_PI);
    for (uint32_t port = 0; port < 32u; ++port) {
        if ((pi & (1u << port)) == 0u) {
            continue;
        }
        uint32_t is = ahci_port_read(ad, port, AHCI_PXIS);
        if (is != 0u) {
            ahci_port_write(ad, port, AHCI_PXIS, is);
        }
    }
    uint32_t global = ahci_read(ad, AHCI_REG_IS);
    if (global != 0u) {
        ahci_write(ad, AHCI_REG_IS, global);
    }
    ad->irq_count = ad->irq_count + 1u;
    return true;
}

static void ahci_dma_teardown(struct o0_ahci *ad) {
    struct o0_dma_buffer *buffers[5] = {
        &ad->cmd_list, &ad->fis_area, &ad->table, &ad->identify, &ad->io
    };
    for (size_t i = 0; i < 5; ++i) {
        ahci_ignore(o0_dma_unpin(buffers[i]));
        ahci_ignore(o0_dma_free(buffers[i]));
    }
    ad->hdr = NULL;
    ad->ct = NULL;
}

static o0_status ahci_dma_setup(struct o0_ahci *ad) {
    struct ahci_buffer_spec {
        struct o0_dma_buffer *buffer;
        size_t size;
        uint32_t alignment;
    } specs[5] = {
        { &ad->cmd_list, 1024u, 1024u },
        { &ad->fis_area, 256u, 256u },
        { &ad->table, 256u, 128u },
        { &ad->identify, 512u, 512u },
        { &ad->io, 4096u, 4096u }
    };

    struct o0_dma_constraint constraint;
    memset(&constraint, 0, sizeof(constraint));
    constraint.addr_mask = 0xFFFFFFFFu; /* AHCI command structures are 32-bit */
    constraint.coherent = true;

    for (size_t i = 0; i < 5; ++i) {
        constraint.alignment = specs[i].alignment;
        o0_status st = o0_dma_alloc(&constraint, specs[i].size, specs[i].buffer);
        if (st != O0_OK) {
            ahci_dma_teardown(ad);
            return st;
        }
        st = o0_dma_pin(specs[i].buffer);
        if (st != O0_OK) {
            ahci_dma_teardown(ad);
            return st;
        }
    }

    memset(ad->cmd_list.virt, 0, ad->cmd_list.size);
    memset(ad->fis_area.virt, 0, ad->fis_area.size);
    memset(ad->table.virt, 0, ad->table.size);
    memset(ad->identify.virt, 0, ad->identify.size);
    memset(ad->io.virt, 0, ad->io.size);
    ad->hdr = (struct ahci_cmd_header *)ad->cmd_list.virt;
    ad->ct = (struct ahci_cmd_table *)ad->table.virt;
    return O0_OK;
}

static bool ahci_port_stop(struct o0_ahci *ad) {
    uint32_t cmd = ahci_port_read(ad, ad->port, AHCI_PXCMD);
    if ((cmd & AHCI_PXCMD_ST) != 0u) {
        ahci_port_write(ad, ad->port, AHCI_PXCMD, cmd & ~AHCI_PXCMD_ST);
        if (!ahci_port_wait(ad, ad->port, AHCI_PXCMD, AHCI_PXCMD_CR, 0u,
                            AHCI_LINK_SPINS)) {
            return false;
        }
    }
    cmd = ahci_port_read(ad, ad->port, AHCI_PXCMD);
    if ((cmd & AHCI_PXCMD_FRE) != 0u) {
        ahci_port_write(ad, ad->port, AHCI_PXCMD, cmd & ~AHCI_PXCMD_FRE);
        if (!ahci_port_wait(ad, ad->port, AHCI_PXCMD, AHCI_PXCMD_FR, 0u,
                            AHCI_LINK_SPINS)) {
            return false;
        }
    }
    return true;
}

static bool ahci_port_start(struct o0_ahci *ad) {
    if (!ahci_port_stop(ad)) {
        return false;
    }
    ahci_port_write(ad, ad->port, AHCI_PXCLB,
                    (uint32_t)(ad->cmd_list.phys & 0xFFFFFFFFu));
    ahci_port_write(ad, ad->port, AHCI_PXCLBU,
                    (uint32_t)(ad->cmd_list.phys >> 32));
    ahci_port_write(ad, ad->port, AHCI_PXFB,
                    (uint32_t)(ad->fis_area.phys & 0xFFFFFFFFu));
    ahci_port_write(ad, ad->port, AHCI_PXFBU,
                    (uint32_t)(ad->fis_area.phys >> 32));
    ahci_port_write(ad, ad->port, AHCI_PXSERR, 0xFFFFFFFFu);
    ahci_port_write(ad, ad->port, AHCI_PXIS, 0xFFFFFFFFu);

    uint32_t cmd = ahci_port_read(ad, ad->port, AHCI_PXCMD);
    ahci_port_write(ad, ad->port, AHCI_PXCMD, cmd | AHCI_PXCMD_FRE);
    if (!ahci_port_wait(ad, ad->port, AHCI_PXCMD, AHCI_PXCMD_FR, AHCI_PXCMD_FR,
                        AHCI_LINK_SPINS)) {
        return false;
    }
    cmd = ahci_port_read(ad, ad->port, AHCI_PXCMD);
    ahci_port_write(ad, ad->port, AHCI_PXCMD, cmd | AHCI_PXCMD_ST);
    return ahci_port_wait(ad, ad->port, AHCI_PXCMD, AHCI_PXCMD_CR,
                          AHCI_PXCMD_CR, AHCI_LINK_SPINS);
}

static o0_block_status ahci_run(struct o0_ahci *ad, uint8_t command, uint64_t lba,
                                uint16_t count, uint64_t data_phys,
                                uint32_t data_len, bool write) {
    uint32_t bit = 1u << ad->port;

    ahci_port_write(ad, ad->port, AHCI_PXIS, 0xFFFFFFFFu);
    ahci_port_write(ad, ad->port, AHCI_PXSERR, 0xFFFFFFFFu);

    memset(ad->hdr, 0, sizeof(*ad->hdr));
    ad->hdr->flags = (uint16_t)(5u | (write ? (1u << 5) : 0u));
    ad->hdr->prdtl = (uint16_t)(data_len != 0u ? 1u : 0u);
    ad->hdr->ctba = (uint32_t)(ad->table.phys & 0xFFFFFFFFu);
    ad->hdr->ctbau = (uint32_t)(ad->table.phys >> 32);

    memset(ad->ct->cfis, 0, sizeof(ad->ct->cfis));
    o0_ahci_encode_h2d(ad->ct->cfis, command, lba, count);
    memset(ad->ct->prdt, 0, sizeof(ad->ct->prdt));
    if (data_len != 0u) {
        ad->ct->prdt[0].dba = (uint32_t)(data_phys & 0xFFFFFFFFu);
        ad->ct->prdt[0].dbau = (uint32_t)(data_phys >> 32);
        ad->ct->prdt[0].dbc = o0_ahci_prdt_dbc(data_len) | (1u << 31);
    }
    __asm__ volatile ("mfence" : : : "memory");

    ahci_port_write(ad, ad->port, AHCI_PXCI, bit);
    bool done = false;
    for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
        if ((ahci_port_read(ad, ad->port, AHCI_PXCI) & bit) == 0u) {
            done = true;
            break;
        }
    }
    if (!done) {
        return O0_BLOCK_ERR_TIMEOUT;
    }

    uint32_t is = ahci_port_read(ad, ad->port, AHCI_PXIS);
    uint32_t tfd = ahci_port_read(ad, ad->port, AHCI_PXTFD);
    ahci_port_write(ad, ad->port, AHCI_PXIS, is);
    if ((is & AHCI_PXIS_TFES) != 0u || (tfd & 0x01u) != 0u) {
        return O0_BLOCK_ERR_IO;
    }
    return O0_BLOCK_OK;
}

static o0_status ahci_identify(struct o0_ahci *ad) {
    if (ahci_run(ad, ATA_IDENTIFY, 0u, 0u, ad->identify.phys, 512u, false) !=
        O0_BLOCK_OK) {
        return O0_EIO;
    }
    const uint16_t *id = (const uint16_t *)ad->identify.virt;
    uint64_t lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                     ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    uint64_t lba28 = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    ad->sectors = lba48 != 0u ? lba48 : lba28;
    ad->sector_size = 512u;
    if ((id[106] & 0x1000u) != 0u) {
        uint32_t shift = id[106] & 0x0Fu;
        if (shift >= 9u && shift <= 12u) {
            ad->sector_size = 1u << shift;
        }
    }
    return ad->sectors != 0u ? O0_OK : O0_ENOTSUP;
}

static o0_status ahci_queue_rq(struct o0_block_queue *queue, struct o0_bio *bio) {
    if (queue == NULL || bio == NULL) {
        return O0_EINVAL;
    }
    struct o0_ahci *ad = (struct o0_ahci *)queue->driver_data;
    if (ad == NULL || !ad->in_use) {
        return O0_ENOENT;
    }

    if (bio->op == O0_BLOCK_FLUSH) {
        o0_block_status st = ahci_run(ad, ATA_FLUSH_CACHE_EXT, 0u, 0u, 0u, 0u,
                                      false);
        o0_block_complete_bio(bio, st, 0u);
        return O0_OK;
    }
    if (bio->op != O0_BLOCK_READ && bio->op != O0_BLOCK_WRITE) {
        return O0_ENOTSUP;
    }
    if (bio->nr_vecs != 1u || bio->vecs == NULL || bio->vecs[0].page == NULL) {
        return O0_EINVAL;
    }

    const struct o0_bio_vec *vec = &bio->vecs[0];
    if (ad->sector_size == 0u || vec->len == 0u ||
        (vec->len % ad->sector_size) != 0u ||
        vec->len / ad->sector_size > 0xFFFFu) {
        return O0_EINVAL;
    }
    uint8_t *buffer = (uint8_t *)vec->page + vec->offset;
    uint64_t phys = o0_virt_to_phys(buffer);
    if (phys == 0u) {
        return O0_EINVAL;
    }

    uint16_t count = (uint16_t)(vec->len / ad->sector_size);
    bool write = bio->op == O0_BLOCK_WRITE;
    o0_block_status st = ahci_run(ad, write ? ATA_WRITE_DMA_EXT : ATA_READ_DMA_EXT,
                                  bio->lba, count, phys, (uint32_t)vec->len, write);
    o0_block_complete_bio(bio, st, st == O0_BLOCK_OK ? vec->len : 0u);
    return O0_OK;
}

static o0_status ahci_reset(struct o0_block_dev *dev) {
    struct o0_ahci *ad = ahci_from_dev(dev);
    if (ad == NULL) {
        return O0_ENOENT;
    }
    return ahci_port_start(ad) ? O0_OK : O0_EIO;
}

static o0_status ahci_flush(struct o0_block_dev *dev) {
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static o0_status ahci_geometry(struct o0_block_dev *dev,
                               struct o0_block_geometry *out) {
    if (dev == NULL || out == NULL) {
        return O0_EINVAL;
    }
    *out = dev->geo;
    return O0_OK;
}

static bool ahci_media_changed(struct o0_block_dev *dev) {
    (void)dev;
    return false;
}

static o0_status ahci_open(struct o0_block_dev *dev, bool exclusive) {
    (void)exclusive;
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static void ahci_close(struct o0_block_dev *dev) {
    (void)dev;
}

static const struct o0_block_ops ahci_block_ops = {
    .reset = ahci_reset,
    .flush = ahci_flush,
    .discard = NULL,
    .write_zeroes = NULL,
    .open = ahci_open,
    .close = ahci_close,
    .media_changed = ahci_media_changed,
    .get_geometry = ahci_geometry,
    .set_features = NULL
};

static const struct o0_block_queue_ops ahci_queue_ops = {
    .queue_rq = ahci_queue_rq,
    .poll = NULL,
    .timeout = NULL
};

static void ahci_set_name(char *dst, const char *src) {
    size_t i = 0;
    for (; i + 1u < O0_BLOCK_NAME_MAX && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int32_t ahci_find_port(struct o0_ahci *ad) {
    uint32_t pi = ahci_read(ad, AHCI_REG_PI);
    for (uint32_t port = 0; port < 32u; ++port) {
        if ((pi & (1u << port)) == 0u) {
            continue;
        }
        uint32_t ssts = ahci_port_read(ad, port, AHCI_PXSSTS);
        if ((ssts & 0x0Fu) != 3u) {
            continue;
        }
        uint32_t sig = ahci_port_read(ad, port, AHCI_PXSIG);
        if (sig != 0x00000101u) {
            continue; /* only plain SATA disks; ATAPI and port multipliers are skipped */
        }
        return (int32_t)port;
    }
    return -1;
}

/* Maps BAR5 (ABAR) uncached. The window is a kernel virtual address, never an
 * HHDM alias, so a later unmap cannot leave a cached alias behind. */
static o0_status ahci_map_abar(const struct o0_pci_device *pci,
                               uint64_t window, volatile uint8_t **out,
                               size_t *out_size) {
    struct o0_pci_bar bar;
    if (o0_pci_probe_bar(pci->bdf, 5u, pci->resources.header_type, &bar) != 0) {
        return O0_EIO;
    }
    if (!bar.valid || !bar.is_memory || bar.base == 0u || bar.size == 0u) {
        return O0_ENOTSUP;
    }
    uint64_t base = bar.base & ~(uint64_t)(O0_PAGE_SIZE - 1u);
    size_t size = (size_t)((bar.base - base) + bar.size);
    if (size < O0_PAGE_SIZE) {
        size = O0_PAGE_SIZE;
    }
    size = (size + O0_PAGE_SIZE - 1u) & ~(O0_PAGE_SIZE - 1u);
    o0_status st = o0_as_map(o0_as_kernel(), window, base, size,
                             O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC | O0_MAP_NX);
    if (st != O0_OK) {
        return st;
    }
    *out = (volatile uint8_t *)(uintptr_t)window;
    *out_size = size;
    return O0_OK;
}

o0_status o0_ahci_probe(const char *name, struct o0_block_dev **out) {
    if (name == NULL || out == NULL || name[0] == '\0') {
        return O0_EINVAL;
    }
    *out = NULL;

    struct o0_ahci *ad = NULL;
    size_t slot = 0;
    for (size_t i = 0; i < AHCI_MAX_DEVICES; ++i) {
        if (!ahci_devices[i].in_use) {
            ad = &ahci_devices[i];
            slot = i;
            break;
        }
    }
    if (ad == NULL) {
        return O0_EBUSY;
    }
    memset(ad, 0, sizeof(*ad));

    ad->vector = O0_VECTOR_NONE;

    /* A q35 machine already has an ich9-ahci controller; a cell that also adds
     * one gets two, and the disk sits on only one of them. Walk every AHCI
     * controller and keep the first with a link-up SATA disk. */
    uint64_t window = AHCI_WINDOW_BASE + (uint64_t)slot * AHCI_WINDOW_STRIDE;
    const struct o0_pci_device *found = NULL;
    int32_t port = -1;
    size_t abar_size = 0;

    for (size_t i = 0; i < o0_pci_device_count(); ++i) {
        const struct o0_pci_device *pci = o0_pci_device(i);
        if (pci == NULL) {
            continue;
        }
        if (pci->class_code != 0x01u || pci->subclass != 0x06u ||
            pci->prog_if != 0x01u) {
            continue;
        }

        uint16_t cmd = 0;
        if (o0_pci_config_read16(pci->bdf, 0x04u, &cmd) != 0) {
            continue;
        }
        cmd = (uint16_t)(cmd | 0x0002u | 0x0004u);
        if (o0_pci_config_write16(pci->bdf, 0x04u, cmd) != 0) {
            continue;
        }

        if (ahci_map_abar(pci, window, &ad->abar, &abar_size) != O0_OK) {
            ad->abar = NULL;
            continue;
        }
        uint32_t cap = ahci_read(ad, AHCI_REG_CAP);
        if (cap == 0u || cap == 0xFFFFFFFFu) {
            (void)o0_as_unmap(o0_as_kernel(), window, abar_size);
            ad->abar = NULL;
            continue;
        }
        ahci_write(ad, AHCI_REG_GHC, ahci_read(ad, AHCI_REG_GHC) | AHCI_GHC_AE);
        int32_t candidate = ahci_find_port(ad);
        if (candidate < 0) {
            (void)o0_as_unmap(o0_as_kernel(), window, abar_size);
            ad->abar = NULL;
            continue;
        }
        found = pci;
        port = candidate;
        break;
    }
    if (found == NULL || ad->abar == NULL) {
        return O0_ENOENT;
    }

    ad->pci = *found;
    ad->port = (uint32_t)port;

    o0_status st = ahci_dma_setup(ad);
    if (st != O0_OK) {
        return st;
    }
    if (!ahci_port_start(ad)) {
        ahci_dma_teardown(ad);
        return O0_EIO;
    }
    if (ahci_identify(ad) != O0_OK) {
        ahci_dma_teardown(ad);
        return O0_ENOTSUP;
    }

    if (found->resources.msi_supported) {
        o0_vector_t vector = O0_VECTOR_NONE;
        if (o0_irq_alloc_vectors(1u, &vector) == O0_OK &&
            o0_irq_register_vector(vector, ahci_irq, ad) == O0_OK &&
            o0_pci_msi_enable(found->bdf, found->resources.msi_cap_offset,
                              o0_lapic_id(), vector) == 0) {
            ad->vector = vector;
            ad->vector_ok = true;
        }
    }
    ahci_port_write(ad, ad->port, AHCI_PXIE, AHCI_PXIE_ERRORS);
    ahci_write(ad, AHCI_REG_GHC, ahci_read(ad, AHCI_REG_GHC) | AHCI_GHC_IE);

    ad->in_use = true;
    ahci_set_name(ad->dev.name, name);
    ad->dev.geo.logical_block_size = ad->sector_size;
    ad->dev.geo.physical_block_size = ad->sector_size;
    ad->dev.geo.nr_sectors = ad->sectors;
    ad->dev.geo.max_hw_sectors = O0_PAGE_SIZE / ad->sector_size;
    ad->dev.geo.max_segments = 1;
    ad->dev.geo.max_segment_size = O0_PAGE_SIZE;
    ad->dev.geo.dma_alignment = ad->sector_size;
    ad->dev.geo.queue_depth = 1;
    ad->dev.geo.features = O0_BLOCK_F_DMA_COHERENT | O0_BLOCK_F_FLUSH;
    ad->dev.nr_queues = 1;
    ad->dev.queues = &ad->queue;
    ad->dev.ops = &ahci_block_ops;
    ad->dev.driver_data = ad;
    ad->queue.id = 0;
    ad->queue.depth = 1;
    ad->queue.ops = &ahci_queue_ops;
    ad->queue.driver_data = ad;

    st = o0_block_register(&ad->dev);
    if (st != O0_OK) {
        ad->in_use = false;
        if (ad->vector_ok) {
            (void)o0_irq_unregister_vector(ad->vector, ahci_irq, ad);
            o0_irq_free_vectors(ad->vector, 1u);
            ad->vector_ok = false;
        }
        ahci_dma_teardown(ad);
        return st;
    }

    *out = &ad->dev;
    return O0_OK;
}

uint64_t o0_ahci_irq_count(struct o0_block_dev *dev) {
    struct o0_ahci *ad = ahci_from_dev(dev);
    return ad != NULL ? ad->irq_count : 0u;
}

o0_status o0_ahci_inject_error(struct o0_block_dev *dev) {
    struct o0_ahci *ad = ahci_from_dev(dev);
    if (ad == NULL) {
        return O0_ENOENT;
    }
    if (ad->sectors < 2u || ad->sector_size == 0u) {
        return O0_ENOTSUP;
    }
    /* Two sectors starting at the last sector: the second one is past the end
     * of the medium. The device must fail the command. */
    uint32_t len = ad->sector_size * 2u;
    o0_block_status st = ahci_run(ad, ATA_READ_DMA_EXT, ad->sectors - 1u, 2u,
                                  ad->io.phys, len, false);
    if (st == O0_BLOCK_OK) {
        return O0_EIO; /* the device accepted an out-of-range read */
    }
    return O0_OK;
}
