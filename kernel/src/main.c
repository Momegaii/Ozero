/* Ozero kernel - entry point and boot sequence.
 *
 * Boot order:
 *   1. serial console, Limine base revision check, exception IDT
 *   2. direct-map offset, boot info, physical page allocator
 *   3. CPU feature detection
 *   4. page tables + CR3 switch (kernel now runs on its own tables)
 *   5. kernel heap (slab), IRQ dispatch, PIC/PIT
 *   6. scheduler, kernel threads, preemption on
 *   7. Stage 3 and Stage 4 self-tests (execution model, then storage)
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#include "ozero/block.h"
#include "ozero/display.h"
#include "ozero/boot.h"
#include "ozero/cpu.h"
#include "ozero/fs.h"
#include "ozero/gpu.h"
#include "ozero/gpu_device.h"
#include "ozero/gpu_hsw.h"
#include "ozero/gpu_queue.h"
#include "ozero/irq.h"
#include "ozero/pci.h"
#include "ozero/ramdisk.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "ozero/thread.h"

#include "serial.h"
#include "string.h"
#include "thread_internal.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start"), aligned(8)))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"), aligned(8)))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt" ::: "memory");
    }
}

__attribute__((noreturn))
static void panic(const char *reason) {
    o0_serial_puts("OZERO: PANIC: ");
    o0_serial_puts(reason);
    o0_serial_puts("\n");
    halt_forever();
}

static const char *memmap_type(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE: return "usable";
        case LIMINE_MEMMAP_RESERVED: return "reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return "acpi-reclaim";
        case LIMINE_MEMMAP_ACPI_NVS: return "acpi-nvs";
        case LIMINE_MEMMAP_BAD_MEMORY: return "bad";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "boot-reclaim";
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return "kernel/modules";
        case LIMINE_MEMMAP_FRAMEBUFFER: return "framebuffer";
        case LIMINE_MEMMAP_RESERVED_MAPPED: return "reserved-mapped";
        default: return "unknown";
    }
}

struct x86_64_exception_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code, rip, cs, rflags;
};

extern void x86_64_idt_init(void);
extern void x86_64_idt_install_irq_gates(void);

void x86_exception_dispatch(struct x86_64_exception_frame *frame) {
    o0_serial_puts("\nOZERO: EXCEPTION vector=");
    o0_serial_dec(frame->vector);
    o0_serial_puts(" error=");
    o0_serial_hex(frame->error_code);
    o0_serial_puts(" rip=");
    o0_serial_hex(frame->rip);
    if (frame->vector == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        o0_serial_puts(" cr2=");
        o0_serial_hex(cr2);
        o0_serial_puts(" pf=");
        o0_serial_puts(frame->error_code & 1u ? "protection-violation" : "page-not-present");
        o0_serial_puts(frame->error_code & 2u ? "-write" : "-read");
        o0_serial_puts(frame->error_code & 4u ? "-user" : "-supervisor");
        o0_serial_puts(frame->error_code & 16u ? "-reserved-bit" : "");
    }
    o0_serial_puts("\n");
    halt_forever();
}

static void report_memory_map(void) {
    struct limine_memmap_response *response = memmap_request.response;
    if (response == NULL || response->entries == NULL) {
        panic("Limine did not provide a memory map");
    }

    o0_serial_puts("OZERO: memory map entries=");
    o0_serial_dec(response->entry_count);
    o0_serial_puts("\n");

    for (uint64_t i = 0; i < response->entry_count; ++i) {
        const struct limine_memmap_entry *entry = response->entries[i];
        if (entry == NULL) {
            panic("memory map contains a null entry");
        }
        o0_serial_puts("  [");
        o0_serial_dec(i);
        o0_serial_puts("] base=");
        o0_serial_hex(entry->base);
        o0_serial_puts(" length=");
        o0_serial_hex(entry->length);
        o0_serial_puts(" type=");
        o0_serial_puts(memmap_type(entry->type));
        o0_serial_puts("\n");
    }
}

extern o0_status o0_boot_populate(const struct limine_memmap_response *);
extern void o0_arch_set_hhdm(uint64_t offset);
extern o0_status o0_paging_init(const struct o0_boot_info *);
extern o0_status o0_paging_load(void);
extern o0_status o0_cpu_detect(void);
extern const struct o0_cpu_features *o0_cpu_features(void);
extern void o0_slab_init(void);
extern uint64_t o0_kheap_total_bytes(void);
extern uint64_t o0_kheap_live_objects(void);
extern o0_status o0_pic_init(void);
extern o0_status o0_pit_start(uint32_t hz);
extern void o0_irq_dispatch_init(void);
extern uint64_t o0_irq_count(o0_gsi_t gsi);

static void stage3_self_test(void) {
    o0_serial_puts("OZERO: stage3 self-test\n");

    /* Kernel heap: distinct allocations, reuse after free, alignment. */
    void *a = o0_kalloc(16, 1);
    void *b = o0_kalloc(200, 8);
    void *c = o0_kalloc(4000, 16);
    if (a == NULL || b == NULL || c == NULL) {
        panic("slab allocation failed");
    }
    if (((uintptr_t)b & 7u) != 0 || ((uintptr_t)c & 15u) != 0) {
        panic("slab alignment violated");
    }
    *(uint32_t *)a = 0xa5a5a5a5u;
    *(uint64_t *)b = 0x1122334455667788ull;
    if (*(uint32_t *)a != 0xa5a5a5a5u || *(uint64_t *)b != 0x1122334455667788ull) {
        panic("slab write/read failed");
    }
    o0_kfree(b, 200);
    void *d = o0_kalloc(200, 8);
    if (d != b) {
        panic("slab did not reuse freed object");
    }
    o0_kfree(a, 16);
    o0_kfree(c, 4000);
    o0_kfree(d, 200);
    o0_serial_puts("OZERO: heap OK total=");
    o0_serial_dec(o0_kheap_total_bytes());
    o0_serial_puts(" live=");
    o0_serial_dec(o0_kheap_live_objects());
    o0_serial_puts("\n");

    /* Address spaces: map, translate, protect, unmap. */
    o0_as_t *as = NULL;
    if (o0_as_create(&as) != O0_OK || as == NULL) {
        panic("address space creation failed");
    }
    o0_paddr_t page = o0_pmm_alloc_pages(0);
    if (page == 0) {
        panic("address space test page allocation failed");
    }
    void *page_virt = o0_phys_to_virt(page);
    if (page_virt == NULL) {
        panic("direct map does not cover allocation");
    }
    *(uint64_t *)page_virt = 0x0f0f0f0f0f0f0f0full;

    const o0_vaddr_t test_addr = 0x40000000ull;
    if (o0_as_map(as, test_addr, page, O0_PAGE_SIZE,
                  O0_MAP_READ | O0_MAP_WRITE) != O0_OK) {
        panic("address space map failed");
    }
    if (o0_virt_to_phys(page_virt) != page) {
        panic("virt_to_phys mismatch for direct map");
    }
    if (o0_as_protect(as, test_addr, O0_PAGE_SIZE, O0_MAP_READ) != O0_OK) {
        panic("address space protect failed");
    }
    if (o0_as_unmap(as, test_addr, O0_PAGE_SIZE) != O0_OK) {
        panic("address space unmap failed");
    }
    if (o0_as_unmap(as, test_addr, O0_PAGE_SIZE) != O0_ENOENT) {
        panic("unmap of an unmapped page did not report ENOENT");
    }
    o0_as_destroy(as);
    o0_pmm_free_pages(page, 0);
    o0_serial_puts("OZERO: address spaces OK\n");

    /* Synchronization primitives. */
    o0_spinlock_t spin = O0_SPINLOCK_INIT;
    o0_spin_init(&spin);
    o0_spin_lock(&spin);
    /* The lock is held by this thread, so a trylock must fail. */
    if (o0_spin_trylock(&spin)) {
        panic("spinlock trylock succeeded on a held lock");
    }
    o0_spin_unlock(&spin);
    if (!o0_spin_trylock(&spin)) {
        panic("spinlock trylock failed on a free lock");
    }
    o0_spin_unlock(&spin);

    o0_mutex_t mutex = O0_MUTEX_INIT;
    o0_mutex_init(&mutex);
    o0_mutex_lock(&mutex);
    if (o0_mutex_trylock(&mutex)) {
        panic("mutex trylock succeeded on a held mutex");
    }
    o0_mutex_unlock(&mutex);
    if (!o0_mutex_trylock(&mutex)) {
        panic("mutex trylock failed on a free mutex");
    }
    o0_mutex_unlock(&mutex);
    o0_serial_puts("OZERO: synchronization OK\n");
    o0_serial_puts("OZERO: stage3 self-test complete\n");
}

/* Stage 4 storage self-test: block layer, RAM disk driver, OzeroFS and its
 * error paths. The RAM disk is the reference driver until a real storage
 * protocol driver and PCI/DMA support exist, so this verifies the block and
 * filesystem contracts, not hardware. */
static void stage4_self_test(void) {
    o0_serial_puts("OZERO: block layer ready devices=");
    o0_serial_dec(o0_block_device_count());
    o0_serial_puts("\n");

    o0_serial_puts("OZERO: pci devices=");
    o0_serial_dec(o0_pci_init());
    o0_serial_putc('\n');
    struct o0_block_dev *disk = NULL;
    if (o0_ramdisk_create("ram0", 2u * 1024u * 1024u, &disk) != O0_OK) {
        panic("RAM disk creation failed");
    }
    o0_serial_puts("OZERO: ramdisk registered name=ram0 blocks=");
    o0_serial_dec(disk->geo.nr_sectors);
    o0_serial_puts("\n");

    if (o0fs_format(disk) != O0_OK) {
        panic("o0fs format failed");
    }
    o0_serial_puts("OZERO: o0fs formatted blocks=");
    o0_serial_dec(disk->geo.nr_sectors);
    o0_serial_puts("\n");

    struct o0fs *fs = NULL;
    if (o0fs_mount(disk, &fs) != O0_OK) {
        panic("o0fs mount failed");
    }
    o0_serial_puts("OZERO: o0fs mounted free blocks=");
    o0_serial_dec(o0fs_free_blocks(fs));
    o0_serial_puts(" inodes=");
    o0_serial_dec(o0fs_free_inodes(fs));
    o0_serial_puts("\n");

    if (o0fs_mkdir(fs, "/sys") != O0_OK ||
        o0fs_create_file(fs, "/sys/log") != O0_OK) {
        panic("o0fs directory creation failed");
    }

    static const char text[] = "o0fs online";
    size_t written = 0;
    if (o0fs_write(fs, "/sys/log", 0, text, sizeof(text) - 1u, &written) !=
            O0_OK ||
        written != sizeof(text) - 1u) {
        panic("o0fs write failed");
    }
    static uint8_t read_back[sizeof(text)];
    size_t got = 0;
    if (o0fs_read(fs, "/sys/log", 0, read_back, sizeof(read_back), &got) !=
            O0_OK ||
        got != sizeof(text) - 1u || memcmp(read_back, text, got) != 0) {
        panic("o0fs read-back mismatch");
    }
    o0_serial_puts("OZERO: o0fs file OK bytes=");
    o0_serial_dec(got);
    o0_serial_puts("\n");

    /* Injected storage error must reach the filesystem, and the device must
     * recover once the fault is cleared. */
    if (o0_ramdisk_inject_fault(disk, O0_BLOCK_ERR_IO, 0, 1) != O0_OK) {
        panic("storage fault injection failed");
    }
    if (o0fs_sync(fs) != O0_EIO) {
        panic("injected storage fault was not reported");
    }
    if (o0_ramdisk_inject_fault(disk, O0_BLOCK_OK, 0, 0) != O0_OK) {
        panic("storage fault clear failed");
    }
    if (o0fs_sync(fs) != O0_OK) {
        panic("storage did not recover after the fault");
    }
    o0_serial_puts("OZERO: o0fs fault test OK\n");

    if (o0fs_unlink(fs, "/sys/log") != O0_OK ||
        o0fs_rmdir(fs, "/sys") != O0_OK) {
        panic("o0fs cleanup failed");
    }
    if (o0fs_unmount(fs) != O0_OK) {
        panic("o0fs unmount failed");
    }
    if (o0_ramdisk_destroy(disk) != O0_OK) {
        panic("RAM disk destruction failed");
    }
    o0_serial_puts("OZERO: storage self-test complete\n");
}

/* Stage 5 Phase 4/5: ring submission, fences, timeout safety and context isolation.
 *
 * Everything here is the software model: no GPU register is written and no device is
 * claimed, so a submitted fence cannot complete on its own. The self-test therefore
 * checks the safety properties that follow from that: a bounded wait times out, the
 * timeout faults the queue, a faulted queue refuses further work, recovery is refused
 * because the Haswell reset sequence is not verified, and teardown of unconsumed work is
 * refused. Address-space isolation is checked by submitting a batch that writes to an
 * address mapped only in a different context's PPGTT.
 */
static uint64_t gpu_clock_now_ns(void *ctx) {
    (void)ctx;
    uint32_t hz = o0_sched_hz();
    if (hz == 0u) {
        return 0u;
    }
    return o0_sched_ticks() * (1000000000ull / hz);
}

static void gpu_clock_sleep_ns(void *ctx, uint64_t ns) {
    (void)ctx;
    o0_thread_sleep_ns(ns);
}

static struct o0_hsw_engine_state gpu_engine;
static struct o0_hsw_engine_binding gpu_binding;
static struct o0_hsw_engine_binding gpu_other_binding;

static void stage5_submission_self_test(void) {
    const struct o0_hsw_page_pool *pool = o0_hsw_kernel_pool();
    struct o0_gpu_memory *ring_mem = NULL;
    struct o0_gpu_memory *batch_mem = NULL;
    struct o0_hsw_ggtt *ggtt = NULL;
    struct o0_hsw_ppgtt *ppgtt = NULL;
    struct o0_hsw_ppgtt *other_ppgtt = NULL;
    struct o0_gpu_vm *vm = NULL;
    struct o0_gpu_vm *other_vm = NULL;
    struct o0_gpu_mapping *mapping = NULL;
    struct o0_gpu_context *ctx = NULL;
    struct o0_gpu_context *other_ctx = NULL;
    struct o0_gpu_queue *queue = NULL;
    struct o0_gpu_queue *other_queue = NULL;

    if (o0_gpu_memory_create_contiguous(0x4000u, &ring_mem) != O0_OK ||
        o0_gpu_memory_create_contiguous(0x1000u, &batch_mem) != O0_OK) {
        panic("GPU submission memory allocation failed");
    }
    if (o0_hsw_ggtt_create(pool, 1024u, &ggtt) != O0_OK) {
        panic("GPU GGTT creation failed");
    }
    for (uint32_t i = 0; i < 4u; ++i) {
        if (o0_hsw_ggtt_map(ggtt, 0x100000ull + (uint64_t)i * 0x1000ull,
                            ring_mem->phys_pages[i], O0_HSW_CACHE_WB_AGE0) != O0_OK) {
            panic("GPU ring GGTT mapping failed");
        }
    }
    if (o0_hsw_ggtt_map(ggtt, 0x200000ull, batch_mem->phys_pages[0],
                        O0_HSW_CACHE_WB_AGE0) != O0_OK) {
        panic("GPU batch GGTT mapping failed");
    }
    if (o0_hsw_engine_init(&gpu_engine, O0_HSW_ENGINE_RENDER, ring_mem, 0x100000ull,
                           4096u) != O0_OK) {
        panic("GPU engine initialisation failed");
    }

    /* Two contexts with independent PPGTTs: only the first maps the write target. */
    if (o0_hsw_ppgtt_create(pool, 0x10000000ull, &ppgtt) != O0_OK ||
        o0_hsw_ppgtt_create(pool, 0x10000000ull, &other_ppgtt) != O0_OK) {
        panic("GPU PPGTT creation failed");
    }
    if (o0_hsw_ppgtt_map(ppgtt, 0x400000ull, batch_mem->phys_pages[0],
                         O0_HSW_CACHE_WB_AGE0) != O0_OK) {
        panic("GPU PPGTT mapping failed");
    }
    o0_hsw_ppgtt_set_active(ppgtt, true);
    o0_hsw_ppgtt_set_active(other_ppgtt, true);

    if (o0_gpu_vm_create(0x0ull, 0x10000000ull, &vm) != O0_OK ||
        o0_gpu_vm_create(0x0ull, 0x10000000ull, &other_vm) != O0_OK) {
        panic("GPU context address space creation failed");
    }
    if (o0_gpu_vm_map(vm, batch_mem, 0x400000ull, O0_GPU_PERM_WRITE, &mapping) != O0_OK) {
        panic("GPU context mapping failed");
    }

    /* NOP, a write to a context-local address, then the batch terminator. */
    uint32_t *batch = batch_mem->backing;
    batch[0] = O0_HSW_MI_NOOP;
    batch[1] = O0_HSW_MI_INSTR(0x20u, 1u); /* MI_STORE_DWORD_IMM */
    batch[2] = 0x400000u;
    batch[3] = 1u;
    batch[4] = O0_HSW_MI_BATCH_BUFFER_END;
    struct o0_gpu_cmd cmd = {
        .memory = batch_mem,
        .gpu_va = 0x200000ull, /* GGTT address of the batch */
        .size_dwords = 1024u,
        .used_dwords = 5u,
    };

    if (o0_gpu_context_create(vm, &ctx) != O0_OK ||
        o0_gpu_context_create(other_vm, &other_ctx) != O0_OK) {
        panic("GPU context creation failed");
    }
    if (o0_hsw_engine_binding_init(&gpu_binding, &gpu_engine, ggtt, ppgtt) != O0_OK ||
        o0_hsw_engine_binding_init(&gpu_other_binding, &gpu_engine, ggtt, other_ppgtt) != O0_OK) {
        panic("GPU engine binding failed");
    }
    if (o0_gpu_queue_create(ctx, o0_hsw_engine_ops(), &gpu_binding, &queue) != O0_OK ||
        o0_gpu_queue_create(other_ctx, o0_hsw_engine_ops(), &gpu_other_binding,
                            &other_queue) != O0_OK) {
        panic("GPU queue creation failed");
    }

    /* Isolation: the second context does not map the write target, so validation fails. */
    uint64_t rejected = 0;
    if (o0_gpu_queue_submit(other_queue, &cmd, &rejected) != O0_ERANGE) {
        panic("GPU context isolation check failed");
    }
    o0_serial_puts("OZERO: gpu context isolation OK\n");

    uint64_t seqno = 0;
    if (o0_gpu_queue_submit(queue, &cmd, &seqno) != O0_OK || seqno != 1u) {
        panic("GPU submission failed");
    }
    o0_serial_puts("OZERO: gpu submit seqno=");
    o0_serial_dec(seqno);
    o0_serial_putc('\n');

    struct o0_gpu_clock clock = {gpu_clock_now_ns, gpu_clock_sleep_ns, NULL};
    if (o0_gpu_queue_wait(queue, seqno, 0u, &clock) != O0_ETIMEDOUT) {
        panic("GPU fence did not time out without a device");
    }
    o0_serial_puts("OZERO: gpu fence timeout OK\n");

    uint64_t again = 0;
    if (o0_gpu_queue_submit(queue, &cmd, &again) != O0_EIO) {
        panic("GPU faulted queue accepted work");
    }
    o0_serial_puts("OZERO: gpu faulted queue refused work\n");

    if (o0_gpu_queue_recover(queue) != O0_ENOTSUP) {
        panic("GPU recovery claimed success without a verified reset");
    }
    o0_serial_puts("OZERO: gpu recovery refused without verified reset\n");

    if (o0_gpu_queue_destroy(queue) != O0_EBUSY) {
        panic("GPU released unconsumed work");
    }
    o0_serial_puts("OZERO: gpu unconsumed work quarantined\n");

    if (o0_gpu_queue_destroy(other_queue) != O0_OK ||
        o0_gpu_context_destroy(other_ctx) != O0_OK) {
        panic("GPU queue teardown failed");
    }
    o0_serial_puts("OZERO: gpu submission self-test complete\n");
}

/* Stage 5 self-test: the GPU-independent core (memory objects, GPU virtual-address
 * allocator) exercised once on the real kernel heap, then GPU discovery over the PCI
 * layer. Discovery never aborts boot: unknown or unsupported devices are recorded, not
 * treated as failures. No GPU register is touched and no page table is installed; this
 * validates Ozero's software model only, not hardware. */
static void stage5_self_test(void) {
    o0_serial_puts("OZERO: stage5 self-test\n");

    /* Phase 2 core: memory object + VA range + mapping lifecycle. */
    struct o0_gpu_memory *mem = NULL;
    if (o0_gpu_memory_create(0x3000u, &mem) != O0_OK || mem == NULL) {
        panic("GPU memory object creation failed");
    }
    struct o0_gpu_vm *vm = NULL;
    if (o0_gpu_vm_create(0x20000000ull, 0x10000000ull, &vm) != O0_OK || vm == NULL) {
        panic("GPU address space creation failed");
    }
    uint64_t va = 0;
    if (o0_gpu_vm_alloc(vm, 0x1000u, 0x1000u, &va) != O0_OK) {
        panic("GPU VA allocation failed");
    }
    struct o0_gpu_mapping *mapping = NULL;
    /* Map at a distinct VA so it does not collide with the reserved allocation. */
    if (o0_gpu_vm_map(vm, mem, 0x20010000ull, O0_GPU_PERM_READ | O0_GPU_PERM_WRITE, &mapping) != O0_OK) {
        panic("GPU mapping failed");
    }
    if (o0_gpu_vm_unmap(vm, mapping) != O0_OK) {
        panic("GPU unmap failed");
    }
    if (o0_gpu_vm_free(vm, va, 0x1000u) != O0_OK) {
        panic("GPU VA free failed");
    }
    o0_gpu_vm_destroy(vm);
    o0_gpu_memory_destroy(mem);
    o0_serial_puts("OZERO: gpu core self-test complete\n");

    /* Phase 1: GPU discovery and capability reporting. */
    size_t count = o0_gpu_discover();
    o0_serial_puts("OZERO: gpu pci display devices=");
    o0_serial_dec(count);
    o0_serial_putc('\n');
    for (size_t i = 0; i < count; ++i) {
        const struct o0_gpu_device *g = o0_gpu_device(i);
        if (g == NULL) {
            continue;
        }
        o0_serial_puts("OZERO: gpu device bdf=");
        o0_serial_dec(g->bdf.bus);
        o0_serial_putc(':');
        o0_serial_dec(g->bdf.device);
        o0_serial_putc('.');
        o0_serial_dec(g->bdf.function);
        o0_serial_puts(" vendor=");
        o0_serial_hex(g->vendor_id);
        o0_serial_puts(" device=");
        o0_serial_hex(g->device_id);
        o0_serial_puts(" class=");
        o0_serial_hex(g->class_code);
        o0_serial_puts(" rev=");
        o0_serial_hex(g->revision);
        o0_serial_puts(" mmio=");
        o0_serial_puts(g->mmio.valid ? "yes" : "no");
        o0_serial_puts(" backend=");
        o0_serial_puts(g->backend == O0_GPU_BACKEND_NONE ? "none" : "haswell");
        o0_serial_puts(" supported=");
        o0_serial_puts(g->supported ? "yes" : "no");
        o0_serial_putc('\n');
    }
    o0_serial_puts("OZERO: gpu discovery complete\n");

    /* Phase 4/5: submission, fences, timeout safety and context isolation. */
    stage5_submission_self_test();
}

static o0_mutex_t demo_mutex = O0_MUTEX_INIT;
static volatile uint32_t demo_counter;

static void worker_thread(void *arg) {
    const char *name = (const char *)arg;
    for (uint32_t i = 0; i < 3; ++i) {
        o0_mutex_lock(&demo_mutex);
        ++demo_counter;
        uint32_t snapshot = demo_counter;
        o0_mutex_unlock(&demo_mutex);
        o0_serial_puts("OZERO: ");
        o0_serial_puts(name);
        o0_serial_puts(" iteration=");
        o0_serial_dec(i);
        o0_serial_puts(" counter=");
        o0_serial_dec(snapshot);
        o0_serial_puts("\n");
        o0_thread_sleep_ns(10000000ull); /* 10 ms */
    }
    o0_serial_puts("OZERO: ");
    o0_serial_puts(name);
    o0_serial_puts(" exiting\n");
}

static void stage3_demo(void) {
    o0_process_t *proc = NULL;
    if (o0_process_create("init", o0_as_kernel(), &proc) != O0_OK) {
        panic("process creation failed");
    }

    o0_mutex_init(&demo_mutex);
    demo_counter = 0;

    static const char *names[2] = { "worker-a", "worker-b" };
    for (size_t i = 0; i < 2; ++i) {
        struct o0_thread_attr attr = {
            .name = names[i],
            .entry = worker_thread,
            .arg = (void *)names[i],
            .stack_size = 0,
            .priority = 0,
            .flags = 0
        };
        o0_thread_t *thread = NULL;
        if (o0_thread_create(proc, &attr, &thread) != O0_OK) {
            panic("kernel thread creation failed");
        }
    }
    o0_serial_puts("OZERO: kernel threads created=");
    o0_serial_dec(o0_thread_count());
    o0_serial_puts("\n");
}

__attribute__((noreturn))
void kmain(void) {
    o0_serial_init();
    o0_serial_puts("\nOZERO: kernel entry reached\n");
    o0_serial_puts("OZERO: architecture=x86_64\n");
    o0_serial_puts("OZERO: boot protocol=Limine\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        panic("bootloader does not support requested Limine base revision 6");
    }

    o0_serial_puts("OZERO: Limine base revision accepted\n");
    x86_64_idt_init();
    o0_serial_puts("OZERO: IDT installed (CPU exceptions 0-31)\n");

    /* Direct-map offset is needed before any page-table walk. */
    if (hhdm_request.response == NULL) {
        panic("Limine did not provide the higher-half direct map offset");
    }
    o0_arch_set_hhdm(hhdm_request.response->offset);
    o0_serial_puts("OZERO: hhdm_offset=");
    o0_serial_hex(hhdm_request.response->offset);
    o0_serial_puts("\n");

    const struct limine_memmap_response *memmap_resp = memmap_request.response;
    if (memmap_resp == NULL || memmap_resp->entries == NULL) {
        panic("Limine did not provide a memory map");
    }
    if (o0_boot_populate(memmap_resp) != O0_OK) {
        panic("failed to populate boot info");
    }
    o0_serial_puts("OZERO: boot info populated\n");

    const struct o0_boot_info *bi = o0_boot_info();
    if (bi == NULL || !o0_boot_contract_ok(bi)) {
        panic("boot info contract check failed");
    }
    if (bi->kernel_phys == 0) {
        panic("kernel physical load address is unknown");
    }
    o0_serial_puts("OZERO: kernel_phys=");
    o0_serial_hex(bi->kernel_phys);
    o0_serial_puts(" kernel_virt=");
    o0_serial_hex(bi->kernel_virt);
    o0_serial_puts(" size=");
    o0_serial_dec(bi->kernel_phys_size);
    o0_serial_puts("\n");

    if (o0_pmm_init(bi) != O0_OK) {
        panic("PMM initialisation failed");
    }
    o0_serial_puts("OZERO: PMM initialised\n");
    o0_serial_puts("OZERO: total memory=");
    o0_serial_dec(o0_pmm_total_bytes());
    o0_serial_puts("\n");
    o0_serial_puts("OZERO: free memory=");
    o0_serial_dec(o0_pmm_free_bytes());
    o0_serial_puts("\n");

    if (o0_cpu_detect() != O0_OK) {
        panic("CPU feature detection failed");
    }
    const struct o0_cpu_features *cf = o0_cpu_features();
    if (cf == NULL) {
        panic("CPU features not available");
    }
    if (!cf->long_mode || !cf->pae) {
        panic("CPU lacks the long-mode baseline (long_mode, pae)");
    }
    o0_serial_puts("OZERO: CPU long_mode=");
    o0_serial_puts(cf->long_mode ? "yes" : "no");
    o0_serial_puts(" pae=");
    o0_serial_puts(cf->pae ? "yes" : "no");
    o0_serial_puts(" nx=");
    o0_serial_puts(cf->nx ? "yes" : "no");
    o0_serial_puts(" sse2=");
    o0_serial_puts(cf->sse2 ? "yes" : "no");
    o0_serial_puts(" apic=");
    o0_serial_puts(cf->apic ? "yes" : "no");
    o0_serial_puts(" page_1gb=");
    o0_serial_puts(cf->page_1gb ? "yes" : "no");
    o0_serial_puts(" invariant_tsc=");
    o0_serial_puts(cf->invariant_tsc ? "yes" : "no");
    o0_serial_puts("\n");

    if (o0_paging_init(bi) != O0_OK) {
        panic("paging initialisation failed");
    }
    o0_serial_puts("OZERO: page tables built\n");

    if (o0_paging_load() != O0_OK) {
        panic("CR3 switch to kernel page tables failed");
    }
    o0_serial_puts("OZERO: page tables active\n");
 
    /* Validate boot framebuffer metadata only. Pixel access remains disabled
     * until an architecture-owned UC/WC mapping is installed. */
    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count != 0 &&
        framebuffer_request.response->framebuffers != NULL &&
        framebuffer_request.response->framebuffers[0] != NULL) {
        const struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
        struct o0_display_mode mode = {
            .address = fb->address, .width = fb->width, .height = fb->height,
            .pitch = fb->pitch, .bytes = 0, .bpp = fb->bpp,
            .memory_model = fb->memory_model,
            .red_size = fb->red_mask_size, .red_shift = fb->red_mask_shift,
            .green_size = fb->green_mask_size, .green_shift = fb->green_mask_shift,
            .blue_size = fb->blue_mask_size, .blue_shift = fb->blue_mask_shift
        };
        o0_status display_status = o0_display_mode_validate(&mode);
        o0_serial_puts("OZERO: framebuffer metadata ");
        o0_serial_puts(display_status == O0_OK ? "valid" : "rejected");
        o0_serial_puts(" width=");
        o0_serial_dec(fb->width);
        o0_serial_puts(" height=");
        o0_serial_dec(fb->height);
        o0_serial_puts(" pitch=");
        o0_serial_dec(fb->pitch);
        o0_serial_puts(" bpp=");
        o0_serial_dec(fb->bpp);
        o0_serial_puts(" (pixel access disabled pending safe mapping)\\n");
    } else {
        o0_serial_puts("OZERO: no boot framebuffer; serial fallback\\n");
    }

    o0_slab_init();
    o0_serial_puts("OZERO: kernel heap ready\n");

    o0_irq_dispatch_init();
    x86_64_idt_install_irq_gates();
    if (o0_pic_init() != O0_OK) {
        panic("PIC initialisation failed");
    }
    o0_serial_puts("OZERO: PIC remapped to vectors 32-47\n");

    if (!o0_thread_init_system()) {
        panic("thread subsystem already initialised");
    }
    o0_sched_init();
    o0_sched_set_hz(100);
    o0_serial_puts("OZERO: scheduler initialised\n");

    if (o0_pit_start(o0_sched_hz()) != O0_OK) {
        panic("PIT initialisation failed");
    }
    /* The scheduler consumes line 0 inside the IRQ layer, so only the PIC mask
     * needs clearing here. */
    if (o0_irq_unmask(0) != O0_OK) {
        panic("failed to unmask the timer IRQ");
    }
    o0_serial_puts("OZERO: timer IRQ enabled at ");
    o0_serial_dec(o0_sched_hz());
    o0_serial_puts(" Hz\n");

    stage3_self_test();
    stage4_self_test();
    stage5_self_test();
    report_memory_map();
    stage3_demo();

    o0_serial_puts("OZERO: boot success; starting scheduler\n");
    o0_sched_start();
}
