#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/virtio_pci_mmio.h"
#include "virtio_mock.h"

#define COMMON_LEN O0_VIRTIO_COMMON_MIN_LENGTH

static void test_mmio_accessors(void) {
    uint8_t bytes[16] = {0};
    uint8_t v8 = 0;
    uint16_t v16 = 0;
    uint32_t v32 = 0;
    uint64_t v64 = 0;

    assert(o0_virtio_mmio_write8(bytes, sizeof(bytes), 1, 0xA5));
    assert(o0_virtio_mmio_read8(bytes, sizeof(bytes), 1, &v8) && v8 == 0xA5);
    assert(o0_virtio_mmio_write16(bytes, sizeof(bytes), 2, 0x1234));
    assert(bytes[2] == 0x34 && bytes[3] == 0x12);
    assert(o0_virtio_mmio_read16(bytes, sizeof(bytes), 2, &v16) && v16 == 0x1234);
    assert(o0_virtio_mmio_write32(bytes, sizeof(bytes), 4, 0x89ABCDEFu));
    assert(bytes[4] == 0xEF && bytes[5] == 0xCD && bytes[6] == 0xAB && bytes[7] == 0x89);
    assert(o0_virtio_mmio_read32(bytes, sizeof(bytes), 4, &v32) && v32 == 0x89ABCDEFu);

    assert(o0_virtio_mmio_write64(bytes, sizeof(bytes), 8, UINT64_C(0x0123456789ABCDEF)));
    assert(o0_virtio_mmio_read64(bytes, sizeof(bytes), 8, &v64) &&
           v64 == UINT64_C(0x0123456789ABCDEF));

    /* Misalignment, width overflow, region overflow and NULL are all refused. */
    assert(!o0_virtio_mmio_read64(bytes, sizeof(bytes), 4, &v64));
    assert(!o0_virtio_mmio_write64(bytes, sizeof(bytes), 12, 0));
    assert(!o0_virtio_mmio_read16(bytes, sizeof(bytes), 1, &v16));
    assert(!o0_virtio_mmio_write32(bytes, sizeof(bytes), 2, 0));
    assert(!o0_virtio_mmio_read32(bytes, sizeof(bytes), 14, &v32));
    assert(!o0_virtio_mmio_write8(bytes, sizeof(bytes), 16, 0));
    assert(!o0_virtio_mmio_read8(NULL, sizeof(bytes), 0, &v8));
    assert(!o0_virtio_mmio_read8(bytes, sizeof(bytes), 0, NULL));
    o0_virtio_mmio_write_barrier();
    o0_virtio_mmio_read_barrier();
}

static void vmock_init(struct vmock *m) {
    memset(m, 0, sizeof(*m));
    m->features[0] = 0x00000200u; /* VIRTIO_BLK_F_FLUSH, bank 0 bit 9 */
    m->features[1] = 0x00000001u; /* VIRTIO_F_VERSION_1, feature bit 32 */
    m->num_queues = 2u;
    m->queue[0].size = 256u;
    m->queue[0].notify_off = 0u;
    m->queue[1].size = 64u;
    m->queue[1].notify_off = 1u;
}

static void test_reset(void) {
    struct vmock m;
    vmock_init(&m);
    m.status = 0xFFu;
    struct o0_virtio_regs regs = {&vmock_iface, &m, COMMON_LEN};
    assert(o0_virtio_reset(&regs) == O0_OK);
    assert(m.status == 0u);

    m.stuck_reset = true;
    m.status = O0_VIRTIO_STATUS_ACKNOWLEDGE;
    assert(o0_virtio_reset(&regs) == O0_ETIMEDOUT);

    /* A missing register interface is an argument error, not a device fault. */
    struct o0_virtio_regs none = {0};
    assert(o0_virtio_reset(&none) == O0_EINVAL);
}

static void test_negotiate(void) {
    struct vmock m;
    vmock_init(&m);
    struct o0_virtio_regs regs = {&vmock_iface, &m, COMMON_LEN};

    /* The driver must accept VERSION_1; anything else is a programming error
     * and must not touch the device. */
    uint32_t low = 0, high = 0;
    uint32_t accesses = m.accesses;
    assert(o0_virtio_negotiate(&regs, 0x200u, 0u, &low, &high) == O0_EINVAL);
    assert(m.accesses == accesses);
    assert(m.status == 0u);

    /* Reading the offer leaves the device in the DRIVER state so the caller can
     * choose what to accept. */
    assert(o0_virtio_features(&regs, &low, &high) == O0_OK);
    assert(low == 0x00000200u && high == 0x00000001u);
    assert(m.status == (O0_VIRTIO_STATUS_ACKNOWLEDGE | O0_VIRTIO_STATUS_DRIVER));
    assert(m.driver_features[0] == 0u && m.driver_features[1] == 0u);

    assert(o0_virtio_negotiate(&regs, 0x200u, 0x1u, &low, &high) == O0_OK);
    /* The two feature banks are distinct: a driver that ignored
     * device_feature_select could not produce these values. */
    assert(low == 0x00000200u);
    assert(high == 0x00000001u);
    assert(m.driver_features[0] == 0x00000200u);
    assert(m.driver_features[1] == 0x00000001u);
    assert(m.status == (O0_VIRTIO_STATUS_ACKNOWLEDGE | O0_VIRTIO_STATUS_DRIVER |
                        O0_VIRTIO_STATUS_FEATURES_OK));

    /* DRIVER_OK is only accepted after FEATURES_OK. */
    assert(o0_virtio_driver_ok(&regs) == O0_OK);
    assert((m.status & O0_VIRTIO_STATUS_DRIVER_OK) != 0u);

    /* A device that does not offer VERSION_1 cannot be driven by this transport. */
    vmock_init(&m);
    m.features[1] = 0u;
    assert(o0_virtio_negotiate(&regs, 0x200u, 0x1u, &low, &high) == O0_ENOTSUP);
    assert((m.status & O0_VIRTIO_STATUS_FAILED) != 0u);

    /* Accepting a feature the device did not offer is refused before any driver
     * feature is programmed. */
    vmock_init(&m);
    assert(o0_virtio_negotiate(&regs, 0x400u, 0x1u, &low, &high) == O0_ENOTSUP);
    assert(m.driver_features[0] == 0u);
    assert((m.status & O0_VIRTIO_STATUS_FAILED) != 0u);

    /* A device that clears FEATURES_OK rejects the feature set. */
    vmock_init(&m);
    m.reject_features_ok = true;
    assert(o0_virtio_negotiate(&regs, 0x200u, 0x1u, &low, &high) == O0_ENOTSUP);
    assert((m.status & O0_VIRTIO_STATUS_FEATURES_OK) == 0u);
    assert(o0_virtio_driver_ok(&regs) == O0_EINVAL);

    /* Reset before negotiation clears the failure state. */
    m.reject_features_ok = false;
    assert(o0_virtio_reset(&regs) == O0_OK);
    assert(o0_virtio_negotiate(&regs, 0x200u, 0x1u, &low, &high) == O0_OK);
}

static void test_register_width_model(void) {
    struct vmock m;
    vmock_init(&m);
    struct o0_virtio_regs regs = {&vmock_iface, &m, COMMON_LEN};

    /* The mock enforces the width each register defines. A byte array would
     * happily answer a 32-bit read of an 8-bit status register. */
    uint32_t v32 = 0;
    uint8_t v8 = 0xFFu;
    assert(!o0_virtio_reg_read32(&regs, O0_VIRTIO_COMMON_DEVICE_STATUS, &v32));
    assert(!o0_virtio_reg_write16(&regs, O0_VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 0u));
    assert(o0_virtio_reg_read8(&regs, O0_VIRTIO_COMMON_DEVICE_STATUS, &v8));
    assert(v8 == 0u);

    /* Out-of-region accesses are refused by the mock as well. */
    assert(!o0_virtio_reg_read32(&regs, COMMON_LEN, &v32));
}

static void test_common_regs_bounds(void) {
    uint8_t bar[COMMON_LEN] = {0};
    struct o0_virtio_pci_caps caps;
    struct o0_virtio_pci_mapping mapping;
    struct o0_virtio_regs regs;

    memset(&caps, 0, sizeof(caps));
    memset(&mapping, 0, sizeof(mapping));
    caps.common.present = true;
    caps.common.bar = 0u;
    caps.common.offset = 0u;
    caps.common.length = COMMON_LEN;
    mapping.bar_virt[0] = bar;
    mapping.bar_length[0] = sizeof(bar);

    assert(o0_virtio_pci_common_regs(&caps, &mapping, &regs));
    assert(regs.base == (volatile void *)bar);
    assert(regs.length == COMMON_LEN);
    assert(regs.iface == &o0_virtio_mmio_reg_iface);

    caps.common.length = COMMON_LEN - 1u;
    assert(!o0_virtio_pci_common_regs(&caps, &mapping, &regs));
    caps.common.length = COMMON_LEN;
    caps.common.offset = 8u;
    assert(!o0_virtio_pci_common_regs(&caps, &mapping, &regs));
    caps.common.offset = 0u;
    mapping.bar_virt[0] = NULL;
    assert(!o0_virtio_pci_common_regs(&caps, &mapping, &regs));
    mapping.bar_virt[0] = bar;
    caps.common.bar = O0_VIRTIO_PCI_CFG_MAX_BARS;
    assert(!o0_virtio_pci_common_regs(&caps, &mapping, &regs));
}

int main(void) {
    test_mmio_accessors();
    test_reset();
    test_negotiate();
    test_register_width_model();
    test_common_regs_bounds();
    puts("virtio modern PCI MMIO and common-config tests: PASS");
    return 0;
}
