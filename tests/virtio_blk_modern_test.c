/* Ozero kernel - modern virtio PCI capability walk host test (Wave 4.2).
 *
 * vblk_modern_walk_caps is pure: it takes a PCI config-space snapshot and
 * reports the common/ISR/device/notify regions from vendor-specific cap ID 0x09.
 * These images are constructed from the virtio PCI spec, not captured from a
 * device; they encode valid layouts and common failure modes (missing cap,
 * incomplete set, self-referential next pointer, wrong config type).
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Only the pure function is exercised here; the rest of the TU never runs on
 * the host. Stubs below satisfy the linker for kernel APIs the driver
 * references but this test never calls; they are placed after the include so
 * every type is already declared by the driver's headers. */
#include "../kernel/src/virtio_blk_modern.c"

void o0_serial_puts(const char *s) { (void)s; }
void o0_serial_dec(uint64_t v) { (void)v; }
void o0_serial_hex(uint64_t v) { (void)v; }

int o0_pci_probe_bar(struct o0_pci_bdf bdf, uint8_t index, uint8_t header_type,
                     struct o0_pci_bar *out) {
    (void)bdf; (void)index; (void)header_type; (void)out;
    return -1;
}

o0_status o0_as_map(o0_as_t *as, o0_vaddr_t window, o0_paddr_t base,
                    size_t size, uint32_t flags) {
    (void)as; (void)window; (void)base; (void)size; (void)flags;
    return O0_OK;
}

o0_status o0_irq_alloc_vectors(uint32_t count, o0_vector_t *out_first) {
    (void)count; (void)out_first;
    return O0_OK;
}
o0_status o0_irq_register_vector(o0_vector_t vector, o0_irq_handler_t handler,
                                 void *ctx) {
    (void)vector; (void)handler; (void)ctx;
    return O0_OK;
}
o0_status o0_irq_unregister_vector(o0_vector_t vector, o0_irq_handler_t fn,
                                   void *ctx) {
    (void)vector; (void)fn; (void)ctx;
    return O0_OK;
}
void o0_irq_free_vectors(o0_vector_t first, uint32_t count) {
    (void)first; (void)count;
}
o0_vector_t o0_lapic_id(void) { return 0; }
int o0_pci_msix_program(volatile void *table, uint16_t table_entries,
                        uint16_t index, uint32_t dest_apic_id, uint32_t vector,
                        bool masked) {
    (void)table; (void)table_entries; (void)index;
    (void)dest_apic_id; (void)vector; (void)masked;
    return 0;
}
int o0_pci_msix_enable(struct o0_pci_bdf bdf, uint8_t cap_offset,
                       volatile void *table, uint16_t index,
                       uint32_t dest_apic_id, uint32_t vector) {
    (void)bdf; (void)cap_offset; (void)table; (void)index;
    (void)dest_apic_id; (void)vector;
    return 0;
}

o0_paddr_t o0_virt_to_phys(const void *vaddr) { (void)vaddr; return 0; }
void o0_block_complete_bio(struct o0_bio *bio, o0_block_status st,
                           size_t bytes) {
    (void)bio; (void)st; (void)bytes;
}
size_t o0_pci_device_count(void) { return 0; }
const struct o0_pci_device *o0_pci_device(size_t idx) { (void)idx; return NULL; }
int o0_pci_config_read32(struct o0_pci_bdf bdf, uint8_t offset, uint32_t *val) {
    (void)bdf; (void)offset; (void)val;
    return -1;
}

#define VIRTIO_PCI_CAP_COMMON_CFG 1u
#define VIRTIO_PCI_CAP_ISR_CFG 2u
#define VIRTIO_PCI_CAP_DEVICE_CFG 3u
#define VIRTIO_PCI_CAP_NOTIFY_CFG 5u
#define PCI_CAP_ID_VNDR 0x09u

static void dump_cfg(uint8_t *cfg, uint8_t cap_off, uint8_t next,
                     uint8_t cfg_type, uint8_t bar, uint32_t offset) {
    struct virtio_pci_cap vc;
    memset(&vc, 0, sizeof(vc));
    vc.cap_vendor = PCI_CAP_ID_VNDR;
    vc.cap_next = next;
    vc.cap_len = (uint8_t)sizeof(vc);
    vc.cfg_type = cfg_type;
    vc.bar = bar;
    vc.offset = offset;
    vc.length = 4096u;
    memcpy(cfg + cap_off, &vc, sizeof(vc));
}

/* Builds a minimal PCI header with a capability list at 0x40 containing one
 * virtio capability per cfg_type. Each capability entry is 16 bytes. */
static void build_caps(uint8_t *cfg, const uint8_t *types, const uint8_t *bars,
                       const uint32_t *offs, size_t n) {
    memset(cfg, 0, 256);
    cfg[0x06] = 0x10u; /* capability list present */
    cfg[0x34] = 0x40u; /* first capability pointer */
    uint8_t cap_off = 0x40u;
    for (size_t i = 0; i < n; ++i) {
        uint8_t next = (i + 1u < n) ? (uint8_t)(cap_off + 16u) : 0x00u;
        dump_cfg(cfg, cap_off, next, types[i], bars[i], offs[i]);
        cap_off += 16u;
    }
}

int main(void) {
    uint8_t cfg[256];
    uint8_t common_bar = 99u, isr_bar = 99u, dev_bar = 99u, notify_bar = 99u;
    uint32_t common_off = 0xFFFFFFFFu, isr_off = 0xFFFFFFFFu;
    uint32_t dev_off = 0xFFFFFFFFu, notify_off = 0xFFFFFFFFu;

    /* ---- valid layout: four caps, each on BAR 0 ---- */
    {
        uint8_t types[] = {VIRTIO_PCI_CAP_COMMON_CFG, VIRTIO_PCI_CAP_ISR_CFG,
                           VIRTIO_PCI_CAP_DEVICE_CFG, VIRTIO_PCI_CAP_NOTIFY_CFG};
        uint8_t bars[] = {0u, 0u, 0u, 0u};
        uint32_t offs[] = {0x1000u, 0x2000u, 0x3000u, 0x4000u};
        build_caps(cfg, types, bars, offs, 4);
        common_bar = isr_bar = dev_bar = notify_bar = 99u;
        common_off = isr_off = dev_off = notify_off = 0xFFFFFFFFu;
        assert(vblk_modern_walk_caps(cfg, &common_bar, &common_off, &isr_bar,
                                     &isr_off, &dev_bar, &dev_off, &notify_bar,
                                     &notify_off));
        assert(common_bar == 0u && common_off == 0x1000u);
        assert(isr_bar == 0u && isr_off == 0x2000u);
        assert(dev_bar == 0u && dev_off == 0x3000u);
        assert(notify_bar == 0u && notify_off == 0x4000u);
    }

    /* ---- no capability list present ---- */
    {
        memset(cfg, 0, 256);
        cfg[0x06] = 0x00u;
        cfg[0x34] = 0x40u;
        assert(!vblk_modern_walk_caps(cfg, &common_bar, &common_off, &isr_bar,
                                      &isr_off, &dev_bar, &dev_off,
                                      &notify_bar, &notify_off));
    }

    /* ---- missing one region (no ISR cap) ---- */
    {
        uint8_t types[] = {VIRTIO_PCI_CAP_COMMON_CFG, VIRTIO_PCI_CAP_DEVICE_CFG,
                           VIRTIO_PCI_CAP_NOTIFY_CFG};
        uint8_t bars[] = {0u, 0u, 0u};
        uint32_t offs[] = {0x1000u, 0x3000u, 0x4000u};
        build_caps(cfg, types, bars, offs, 3);
        assert(!vblk_modern_walk_caps(cfg, &common_bar, &common_off, &isr_bar,
                                      &isr_off, &dev_bar, &dev_off,
                                      &notify_bar, &notify_off));
    }

    /* ---- self-referential next pointer ---- */
    {
        memset(cfg, 0, 256);
        cfg[0x06] = 0x10u;
        cfg[0x34] = 0x40u;
        cfg[0x40] = PCI_CAP_ID_VNDR;
        cfg[0x41] = 0x40u; /* points to itself */
        dump_cfg(cfg, 0x40u, 0x40u, VIRTIO_PCI_CAP_COMMON_CFG, 0u, 0x1000u);
        assert(!vblk_modern_walk_caps(cfg, &common_bar, &common_off, &isr_bar,
                                      &isr_off, &dev_bar, &dev_off,
                                      &notify_bar, &notify_off));
    }

    /* ---- wrong config type is skipped, not matched ---- */
    {
        uint8_t types[] = {VIRTIO_PCI_CAP_COMMON_CFG, 99u,
                           VIRTIO_PCI_CAP_ISR_CFG, VIRTIO_PCI_CAP_DEVICE_CFG,
                           VIRTIO_PCI_CAP_NOTIFY_CFG};
        uint8_t bars[] = {0u, 0u, 0u, 0u, 0u};
        uint32_t offs[] = {0x1000u, 0x1100u, 0x2000u, 0x3000u, 0x4000u};
        build_caps(cfg, types, bars, offs, 5);
        common_bar = isr_bar = dev_bar = notify_bar = 99u;
        common_off = isr_off = dev_off = notify_off = 0xFFFFFFFFu;
        assert(vblk_modern_walk_caps(cfg, &common_bar, &common_off, &isr_bar,
                                     &isr_off, &dev_bar, &dev_off, &notify_bar,
                                     &notify_off));
        assert(isr_off == 0x2000u); /* bogus 0x1100u entry was skipped */
    }

    printf("virtio_blk_modern_test: all capability-walk assertions passed\n");
    return 0;
}
