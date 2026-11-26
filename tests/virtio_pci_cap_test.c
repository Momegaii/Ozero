#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ozero/virtio_pci_cap.h"

static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void cap(uint8_t *c, uint8_t at, uint8_t next, uint8_t type,
                uint8_t bar, uint32_t off, uint32_t len) {
    c[at]=O0_VIRTIO_PCI_CAP_VENDOR; c[at+1]=next; c[at+2]=16; c[at+3]=type;
    c[at+4]=bar; put32(c+at+8,off); put32(c+at+12,len);
}
static void valid_dump(uint8_t *c) {
    memset(c,0,256); c[6]=0x10; c[0x34]=0x40;
    cap(c,0x40,0x50,O0_VIRTIO_PCI_CAP_COMMON,0,0x000,56);
    cap(c,0x50,0x64,O0_VIRTIO_PCI_CAP_NOTIFY,2,0x100,64);
    c[0x52]=20; put32(c+0x60,4); /* notify cap length and multiplier */
    cap(c,0x64,0x74,O0_VIRTIO_PCI_CAP_ISR,1,0x000,1);
    cap(c,0x74,0,O0_VIRTIO_PCI_CAP_DEVICE,3,0x000,64);
    c[0x76]=16;
}
int main(void) {
    uint8_t cfg[256]; uint32_t bars[6]={4096,4096,4096,4096,0,0};
    struct o0_virtio_pci_caps out;
    valid_dump(cfg);
    assert(o0_virtio_pci_parse_caps(cfg,sizeof(cfg),bars,&out));
    assert(out.common.bar==0 && out.common.length==56);
    assert(out.notify.bar==2 && out.notify.offset==0x100);
    assert(out.notify_multiplier==4 && out.isr.length==1 && out.device.bar==3);

    struct o0_pci_bar metadata[6];
    memset(metadata, 0, sizeof(metadata));
    for (size_t i = 0; i < 6; ++i) {
        metadata[i].valid = true;
        metadata[i].is_memory = true;
        metadata[i].assigned = true;
        metadata[i].size = bars[i];
    }
    assert(o0_virtio_pci_validate_bars(&out, metadata));
    metadata[2].is_memory = false; /* an I/O BAR cannot back a capability */
    assert(!o0_virtio_pci_validate_bars(&out, metadata));
    metadata[2].is_memory = true;
    metadata[0].assigned = false;
    assert(!o0_virtio_pci_validate_bars(&out, metadata));
    metadata[0].assigned = true;
    metadata[3].valid = false;
    assert(!o0_virtio_pci_validate_bars(&out, metadata));
    metadata[3].valid = true;
    metadata[1].size = 0;
    assert(!o0_virtio_pci_validate_bars(&out, metadata));

    /* Missing list, malformed alignment, cycle, duplicate required type. */
    cfg[6]=0;
    assert(!o0_virtio_pci_parse_caps(cfg,sizeof(cfg),bars,&out));
    valid_dump(cfg); cfg[0x34]=0x41;
    assert(!o0_virtio_pci_parse_caps(cfg,sizeof(cfg),bars,&out));
    valid_dump(cfg); cfg[0x50+1]=0x40;
    assert(!o0_virtio_pci_parse_caps(cfg,sizeof(cfg),bars,&out));
    valid_dump(cfg); cap(cfg,0x84,0x94,O0_VIRTIO_PCI_CAP_COMMON,0,0,56);
    cfg[0x50+1]=0x84;
    assert(!o0_virtio_pci_parse_caps(cfg,sizeof(cfg),bars,&out));

    /* BAR index, zero length and range overflow are rejected. */
    valid_dump(cfg); cfg[0x40+4]=6;
    assert(!o0_virtio_pci_parse_caps(cfg,sizeof(cfg),bars,&out));
    valid_dump(cfg); put32(cfg+0x40+12,0);
    assert(!o0_virtio_pci_parse_caps(cfg,sizeof(cfg),bars,&out));
    valid_dump(cfg); put32(cfg+0x40+8,4090); put32(cfg+0x40+12,16);
    assert(!o0_virtio_pci_parse_caps(cfg,sizeof(cfg),bars,&out));

    assert(!o0_virtio_pci_parse_caps(NULL,sizeof(cfg),bars,&out));
    assert(!o0_virtio_pci_parse_caps(cfg,sizeof(cfg),NULL,&out));
    puts("virtio modern PCI capability parser tests: PASS");
    return 0;
}
