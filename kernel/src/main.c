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

#include "ozero/acpi.h"
#include "ozero/apic_timer.h"
#include "ozero/block.h"
#include "ozero/budget.h"
#include "ozero/display.h"
#include "ozero/compositor.h"
#include "ozero/shell.h"
#include "ozero/surface.h"
#include "ozero/window.h"
#include "ozero/endpoint.h"
#include "ozero/boot.h"
#include "ozero/cpu.h"
#include "ozero/cpu_local.h"
#include "ozero/fs.h"
#include "ozero/gdt.h"
#include "ozero/gpu.h"
#include "ozero/gpu_device.h"
#include "ozero/gpu_hsw.h"
#include "ozero/gpu_queue.h"
#include "ozero/handle.h"
#include "ozero/input_ps2.h"
#include "ozero/ioapic.h"
#include "ozero/kaslr.h"
#include "ozero/memory.h"
#include "ozero/object.h"
#include "ozero/irq.h"
#include "ozero/pci.h"
#include "ozero/ramdisk.h"
#include "ozero/mm.h"
#include "ozero/smp.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "ozero/thread.h"
#include "ozero/uapi.h"
#include "ozero/user.h"

#include "serial.h"

#ifndef O0_BUILD_COMMIT
#define O0_BUILD_COMMIT "unknown"
#endif
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

/* Present only when the firmware that loaded Limine is UEFI. Limine leaves the
 * response null under BIOS, so this is the firmware discriminator reported in
 * the boot log and asserted by the UEFI smoke test. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .flags = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_efi_system_table_request efi_system_table_request = {
    .id = LIMINE_EFI_SYSTEM_TABLE_REQUEST_ID,
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
extern void x86_64_idt_install_ipi_gate(void);
extern void x86_64_idt_install_apic_timer_gate(void);
extern void x86_64_idt_install_double_fault_ist(void);
extern bool o0_usercopy_fixup(uint64_t fault_rip, uint64_t *fixup_rip);

void x86_exception_dispatch(struct x86_64_exception_frame *frame) {
    /* A fault taken from ring 3 must never take the kernel down: the process is
     * killed and the scheduler continues. */
    if ((frame->cs & 3u) == 3u) {
        uint64_t cr2 = 0;
        if (frame->vector == 14) {
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        }
        o0_thread_user_fault(frame->vector, frame->rip, cr2,
                             frame->error_code);
    }

    /* A fault inside a usercopy core is recovered by redirecting execution to
     * the fixup label, which returns -EFAULT to the syscall layer. */
    if (frame->vector == 14) {
        uint64_t fixup = 0;
        if (o0_usercopy_fixup(frame->rip, &fixup)) {
            frame->rip = fixup;
            return;
        }
    }

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
extern uint64_t o0_arch_hhdm_offset(void);
extern o0_status o0_paging_init(const struct o0_boot_info *);
extern o0_status o0_paging_load(void);
extern o0_status o0_cpu_detect(void);
extern const struct o0_cpu_features *o0_cpu_features(void);
extern void o0_slab_init(void);
extern uint64_t o0_kheap_total_bytes(void);
extern uint64_t o0_kheap_live_objects(void);
extern o0_status o0_pic_init(void);
extern bool o0_pic_apic_extint_active(void);
extern o0_status o0_pit_start(uint32_t hz);
extern void o0_irq_dispatch_init(void);
extern uint64_t o0_irq_count(o0_gsi_t gsi);
extern void o0_input_ps2_init(void);

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
    if (o0_as_create(o0_budget_root(), &as) != O0_OK || as == NULL) {
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
    if (o0_process_create("init", o0_as_kernel(), o0_budget_root(), &proc) != O0_OK) {
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

/* ---------- Wave 2: ring-3 entry and contained failures ---------- */

extern const unsigned char _binary_build_user_init_elf_start[];
extern const unsigned char _binary_build_user_init_elf_end[];
extern const unsigned char _binary_build_user_quiet_elf_start[];
extern const unsigned char _binary_build_user_quiet_elf_end[];
extern const unsigned char _binary_build_user_fault_null_elf_start[];
extern const unsigned char _binary_build_user_fault_null_elf_end[];
extern const unsigned char _binary_build_user_fault_priv_elf_start[];
extern const unsigned char _binary_build_user_fault_priv_elf_end[];
extern const unsigned char _binary_build_user_fault_kjump_elf_start[];
extern const unsigned char _binary_build_user_fault_kjump_elf_end[];
extern const unsigned char _binary_build_user_badptr_elf_start[];
extern const unsigned char _binary_build_user_badptr_elf_end[];

static int wave2_run_user(const char *name, const unsigned char *start,
                          const unsigned char *end) {
    size_t size = (size_t)(end - start);
    o0_serial_puts("OZERO: wave2 run ");
    o0_serial_puts(name);
    o0_serial_putc('\n');
    o0_thread_t *thread = NULL;
    o0_status spawn_status = o0_user_spawn(name, start, size, &thread);
    if (spawn_status != O0_OK) {
        o0_serial_puts("OZERO: wave2 spawn status=");
        o0_serial_puts(o0_status_name(spawn_status));
        o0_serial_putc('\n');
        panic("user image load failed");
    }
    int status = -1234;
    if (o0_thread_join(thread, &status) != O0_OK) {
        panic("user thread join failed");
    }
    o0_serial_puts("OZERO: wave2 done ");
    o0_serial_puts(name);
    o0_serial_puts(" status=");
    o0_serial_int((int64_t)status);
    o0_serial_putc('\n');
    return status;
}

static void wave3_self_test(void *arg);

static void wave2_self_test(void *arg) {
    (void)arg;

    if (wave2_run_user("init", _binary_build_user_init_elf_start,
                       _binary_build_user_init_elf_end) != 0) {
        panic("init process did not exit with status 0");
    }
    o0_serial_puts("OZERO: syscall exit status=0\n");

    /* A NULL dereference must kill only the faulting process. */
    if (wave2_run_user("fault-null", _binary_build_user_fault_null_elf_start,
                       _binary_build_user_fault_null_elf_end) != -1) {
        panic("NULL-dereference process was not killed");
    }
    /* A privileged instruction from ring 3 must be contained. */
    if (wave2_run_user("fault-priv", _binary_build_user_fault_priv_elf_start,
                       _binary_build_user_fault_priv_elf_end) != -1) {
        panic("privileged-instruction process was not killed");
    }
    /* A jump into the kernel half must be contained. */
    if (wave2_run_user("fault-kjump", _binary_build_user_fault_kjump_elf_start,
                       _binary_build_user_fault_kjump_elf_end) != -1) {
        panic("kernel-address jump was not contained");
    }
    /* A bad syscall pointer must be rejected with -EFAULT, not fault the kernel. */
    if (wave2_run_user("badptr", _binary_build_user_badptr_elf_start,
                       _binary_build_user_badptr_elf_end) != 0) {
        panic("bad syscall pointer was not rejected with -EFAULT");
    }
    o0_serial_puts("OZERO: syscall bad pointer rejected\n");

    /* Process churn: every allocation must come back. */
    uint64_t before = o0_pmm_free_bytes();
    for (uint32_t i = 0; i < 16u; ++i) {
        if (wave2_run_user("churn", _binary_build_user_quiet_elf_start,
                           _binary_build_user_quiet_elf_end) != 0) {
            panic("churn process failed");
        }
    }
    uint64_t after = o0_pmm_free_bytes();
    if (after != before) {
        panic("process churn leaked physical pages");
    }
    o0_serial_puts("OZERO: user churn OK processes=16\n");
    o0_serial_puts("OZERO: wave2 self-test complete\n");

    wave3_self_test(NULL);
}

/* ---------- Wave 3: handles, budgets and synchronous IPC ---------- */

extern const unsigned char _binary_build_user_spawner_elf_start[];
extern const unsigned char _binary_build_user_spawner_elf_end[];
extern const unsigned char _binary_build_user_spawn_child_elf_start[];
extern const unsigned char _binary_build_user_spawn_child_elf_end[];
extern const unsigned char _binary_build_user_ipc_server_elf_start[];
extern const unsigned char _binary_build_user_ipc_server_elf_end[];
extern const unsigned char _binary_build_user_ipc_client_elf_start[];
extern const unsigned char _binary_build_user_ipc_client_elf_end[];
extern const unsigned char _binary_build_user_handles_elf_start[];
extern const unsigned char _binary_build_user_handles_elf_end[];
extern const unsigned char _binary_build_user_budget_elf_start[];
extern const unsigned char _binary_build_user_budget_elf_end[];
extern const unsigned char _binary_build_user_ipcneg_elf_start[];
extern const unsigned char _binary_build_user_ipcneg_elf_end[];
extern const unsigned char _binary_build_user_peer_server_elf_start[];
extern const unsigned char _binary_build_user_peer_server_elf_end[];

static struct o0_memory *wave3_image(const unsigned char *start,
                                     const unsigned char *end) {
    size_t size = (size_t)(end - start);
    struct o0_memory *memory = NULL;
    if (o0_memory_create_from(o0_budget_root(), start, size,
                              O0_RIGHT_MEMORY_IMAGE | O0_RIGHT_TRANSFER,
                              &memory) != O0_OK) {
        panic("wave3 image object creation failed");
    }
    return memory;
}

static o0_thread_t *wave3_spawn_image(const char *name,
                                      const unsigned char *start,
                                      const unsigned char *end,
                                      const struct o0_spawn_grant *extra,
                                      size_t extra_count, uint32_t flags,
                                      bool grant_image) {
    struct o0_memory *image = wave3_image(start, end);
    struct o0_spawn_grant grants[8];
    size_t count = 0;
    if (grant_image) {
        o0_object_ref(&image->obj);
        grants[count].object = &image->obj;
        grants[count].rights = O0_RIGHT_READ | O0_RIGHT_MAP | O0_RIGHT_INFO;
        grants[count].reserved = 0;
        ++count;
    }
    if (count + extra_count > O0_ARRAY_SIZE(grants)) {
        panic("too many spawn grants");
    }
    for (size_t i = 0; i < extra_count; ++i) {
        grants[count + i] = extra[i];
    }
    count += extra_count;

    o0_thread_t *thread = NULL;
    o0_status st = o0_user_spawn_memory(name, image, o0_budget_root(), grants,
                                        count, flags, &thread);
    o0_object_unref(&image->obj);
    if (st != O0_OK) {
        o0_serial_puts("OZERO: wave3 spawn failed ");
        o0_serial_puts(name);
        o0_serial_puts(" status=");
        o0_serial_puts(o0_status_name(st));
        o0_serial_putc('\n');
        panic("wave3 user spawn failed");
    }
    return thread;
}

static o0_thread_t *wave3_spawn(const char *name, const unsigned char *start,
                                const unsigned char *end,
                                const struct o0_spawn_grant *extra,
                                size_t extra_count, uint32_t flags) {
    return wave3_spawn_image(name, start, end, extra, extra_count, flags, false);
}

static int wave3_join(o0_thread_t *thread, const char *name) {
    int status = -1234;
    if (o0_thread_join(thread, &status) != O0_OK) {
        panic("wave3 join failed");
    }
    o0_serial_puts("OZERO: wave3 done ");
    o0_serial_puts(name);
    o0_serial_puts(" status=");
    o0_serial_int((int64_t)status);
    o0_serial_putc('\n');
    return status;
}

static void wave3_wait_threads(uint32_t target) {
    uint64_t deadline = o0_sched_ticks() + (uint64_t)o0_sched_hz() * 3u;
    while (o0_thread_count() > target) {
        if (o0_sched_ticks() > deadline) {
            panic("wave3 detached child did not exit");
        }
        o0_thread_yield();
    }
}

static void wave3_run_ipc_pair(const char *tag) {
    struct o0_endpoint *endpoint = NULL;
    if (o0_endpoint_create(o0_budget_root(), false, &endpoint) != O0_OK) {
        panic("wave3 endpoint creation failed");
    }

    struct o0_spawn_grant server_grant;
    server_grant.object = &endpoint->obj;
    server_grant.rights = O0_RIGHT_SEND | O0_RIGHT_RECV | O0_RIGHT_INFO;
    server_grant.reserved = 0;
    o0_object_ref(&endpoint->obj);
    o0_thread_t *server = wave3_spawn("ipc-server",
                                      _binary_build_user_ipc_server_elf_start,
                                      _binary_build_user_ipc_server_elf_end,
                                      &server_grant, 1, 0);

    struct o0_spawn_grant client_grant = server_grant;
    o0_object_ref(&endpoint->obj);
    o0_thread_t *client = wave3_spawn("ipc-client",
                                      _binary_build_user_ipc_client_elf_start,
                                      _binary_build_user_ipc_client_elf_end,
                                      &client_grant, 1, 0);
    o0_object_unref(&endpoint->obj);

    if (wave3_join(server, tag) != 0 || wave3_join(client, tag) != 0) {
        panic("wave3 IPC pair failed");
    }
    o0_serial_puts("OZERO: ipc pair OK ");
    o0_serial_puts(tag);
    o0_serial_putc('\n');
}


static volatile uint32_t smp_seen[O0_MAX_CPUS];
static volatile uint32_t smp_iterations;
static volatile uint32_t smp_migrations;
static volatile uint32_t smp_stop;

static void smp_worker(void *arg) {
    uint32_t target = (uint32_t)(uintptr_t)arg;
    uint32_t online = o0_smp_online_count();
    while (__atomic_load_n(&smp_stop, __ATOMIC_ACQUIRE) == 0u) {
        uint32_t cpu = o0_cpu_index();
        if (cpu < O0_MAX_CPUS) {
            __atomic_add_fetch(&smp_seen[cpu], 1u, __ATOMIC_RELEASE);
        }
        __atomic_add_fetch(&smp_iterations, 1u, __ATOMIC_RELEASE);
        if (online > 1u) {
            target = (target + 1u) % online;
            o0_status migrated = o0_thread_migrate(o0_thread_self(), target);
            if (migrated != O0_OK && migrated != O0_ENOTSUP) {
                panic("smp worker migration failed");
            }
            if (migrated == O0_OK) {
                __atomic_add_fetch(&smp_migrations, 1u, __ATOMIC_RELEASE);
            }
        }
        for (uint32_t spin = 0; spin < 64u; ++spin) {
            o0_cpu_relax();
        }
        o0_thread_yield();
    }
    uint32_t cpu = o0_cpu_index();
    if (cpu < O0_MAX_CPUS) {
        __atomic_add_fetch(&smp_seen[cpu], 1u, __ATOMIC_RELEASE);
    }
}

static void wave3_smp_stress(void) {
    uint32_t online = o0_smp_online_count();
    for (uint32_t i = 0; i < O0_MAX_CPUS; ++i) {
        smp_seen[i] = 0;
    }
    smp_iterations = 0;
    smp_migrations = 0;
    smp_stop = 0;
    if (online <= 1u) {
        o0_serial_puts("OZERO: smp scheduling skipped online=1\n");
        return;
    }

    o0_process_t *proc = NULL;
    if (o0_process_create("smp-stress", o0_as_kernel(), o0_budget_root(), &proc) != O0_OK) {
        panic("smp stress process creation failed");
    }
    o0_thread_t *threads[O0_MAX_CPUS];
    uint32_t count = 0;
    for (uint32_t i = 0; i < online && i < O0_MAX_CPUS; ++i) {
        struct o0_thread_attr attr = {
            .name = "smp-worker",
            .entry = smp_worker,
            .arg = (void *)(uintptr_t)((i + 1u) % online),
            .stack_size = 0,
            .priority = 0,
            .flags = 0
        };
        if (o0_thread_create(proc, &attr, &threads[count]) != O0_OK) {
            panic("smp stress thread creation failed");
        }
        if (o0_thread_migrate(threads[count], i) != O0_OK) {
            panic("smp stress initial migration failed");
        }
        ++count;
    }

    /* Let the migration workers reach every CPU before the allocation
     * baseline, so only the user churn changes the PMM free count. */
    o0_thread_sleep_ns(1000000ull);

    uint64_t before = o0_pmm_free_bytes();
    for (uint32_t i = 0; i < 16u; ++i) {
        if (wave2_run_user("smp-churn", _binary_build_user_quiet_elf_start,
                           _binary_build_user_quiet_elf_end) != 0) {
            panic("smp churn process failed");
        }
    }
    uint64_t after = o0_pmm_free_bytes();
    if (after != before) {
        panic("smp user churn leaked physical pages");
    }
    o0_serial_puts("OZERO: smp user churn OK processes=16\n");

    __atomic_store_n(&smp_stop, 1u, __ATOMIC_RELEASE);
    for (uint32_t i = 0; i < count; ++i) {
        if (o0_thread_join(threads[i], NULL) != O0_OK) {
            panic("smp stress join failed");
        }
    }
    uint32_t seen = 0;
    for (uint32_t i = 0; i < O0_MAX_CPUS; ++i) {
        if (smp_seen[i] != 0u) {
            ++seen;
        }
    }
    if (seen < 2u || smp_migrations == 0u) {
        panic("smp stress never migrated across two CPUs");
    }
    o0_serial_puts("OZERO: smp scheduling OK cpus=");
    o0_serial_dec(seen);
    o0_serial_puts(" threads=");
    o0_serial_dec(count);
    o0_serial_puts(" iterations=");
    o0_serial_dec(smp_iterations);
    o0_serial_puts(" migrations=");
    o0_serial_dec(smp_migrations);
    o0_serial_puts("\n");
}

static void wave3_self_test(void *arg) {
    (void)arg;

    o0_serial_puts("OZERO: handle table ready\n");
    o0_serial_puts("OZERO: budget accounting ready\n");

    /* Spawner: user space splits a budget and spawns a child through the
     * process_spawn syscall. The child is detached, so it is reaped by the
     * scheduler after it exits. */
    struct o0_budget *spawner_budget = NULL;
    if (o0_budget_split(o0_budget_root(), 8u << 20, &spawner_budget) != O0_OK) {
        panic("wave3 spawner budget split failed");
    }
    struct o0_spawn_grant spawner_grants[1];
    spawner_grants[0].object = &spawner_budget->obj;
    spawner_grants[0].rights = O0_RIGHT_SPLIT | O0_RIGHT_INFO;
    spawner_grants[0].reserved = 0;
    uint32_t baseline_threads = o0_thread_count();
    o0_thread_t *spawner = wave3_spawn_image(
        "spawner", _binary_build_user_spawner_elf_start,
        _binary_build_user_spawner_elf_end, spawner_grants, 1, 0, true);
    if (wave3_join(spawner, "spawner") != 0) {
        panic("spawner did not exit with status 0");
    }
    o0_object_unref(&spawner_budget->obj);
    wave3_wait_threads(baseline_threads);
    o0_serial_puts("OZERO: spawn syscall child reaped\n");

    /* Request/reply over a synchronous endpoint. */
    wave3_run_ipc_pair("pair-1");
    o0_serial_puts("OZERO: ipc self-test complete\n");

    /* Handle, rights, budget and memory negative tests. */
    struct o0_endpoint *endpoint = NULL;
    if (o0_endpoint_create(o0_budget_root(), false, &endpoint) != O0_OK) {
        panic("wave3 handle endpoint creation failed");
    }
    struct o0_budget *handle_budget = NULL;
    if (o0_budget_split(o0_budget_root(), 4u << 20, &handle_budget) != O0_OK) {
        panic("wave3 handle budget split failed");
    }
    struct o0_memory *handle_memory = NULL;
    if (o0_memory_create(o0_budget_root(), O0_PAGE_SIZE,
                         O0_RIGHT_MEMORY_DEFAULT, &handle_memory) != O0_OK) {
        panic("wave3 handle memory creation failed");
    }
    struct o0_spawn_grant handle_grants[3];
    handle_grants[0].object = &endpoint->obj;
    handle_grants[0].rights = O0_RIGHT_ENDPOINT_DEFAULT;
    handle_grants[0].reserved = 0;
    handle_grants[1].object = &handle_budget->obj;
    handle_grants[1].rights = O0_RIGHT_BUDGET_DEFAULT;
    handle_grants[1].reserved = 0;
    handle_grants[2].object = &handle_memory->obj;
    handle_grants[2].rights = O0_RIGHT_MEMORY_DEFAULT;
    handle_grants[2].reserved = 0;
    o0_thread_t *handles = wave3_spawn("handles",
                                       _binary_build_user_handles_elf_start,
                                       _binary_build_user_handles_elf_end,
                                       handle_grants, 3, 0);
    if (wave3_join(handles, "handles") != 0) {
        panic("handle negative tests failed");
    }

    /* Budget exhaustion and return to baseline. */
    o0_thread_t *budget = wave3_spawn("budget",
                                      _binary_build_user_budget_elf_start,
                                      _binary_build_user_budget_elf_end,
                                      NULL, 0, 0);
    if (wave3_join(budget, "budget") != 0) {
        panic("budget exhaustion test failed");
    }

    /* Peer death: the server exits before the client runs. */
    struct o0_endpoint *dead_endpoint = NULL;
    if (o0_endpoint_create(o0_budget_root(), false, &dead_endpoint) != O0_OK) {
        panic("wave3 dead endpoint creation failed");
    }
    struct o0_spawn_grant peer_grant;
    peer_grant.object = &dead_endpoint->obj;
    peer_grant.rights = O0_RIGHT_SEND | O0_RIGHT_RECV | O0_RIGHT_INFO;
    peer_grant.reserved = 0;
    o0_object_ref(&dead_endpoint->obj);
    o0_thread_t *peer = wave3_spawn("peer-server",
                                    _binary_build_user_peer_server_elf_start,
                                    _binary_build_user_peer_server_elf_end,
                                    &peer_grant, 1, 0);
    if (wave3_join(peer, "peer-server") != 0) {
        panic("peer server did not exit with status 0");
    }
    struct o0_budget *neg_budget = NULL;
    if (o0_budget_split(o0_budget_root(), 4u << 20, &neg_budget) != O0_OK) {
        panic("wave3 negative budget split failed");
    }
    struct o0_spawn_grant neg_grants[2];
    neg_grants[0].object = &dead_endpoint->obj;
    neg_grants[0].rights = O0_RIGHT_SEND | O0_RIGHT_RECV | O0_RIGHT_INFO;
    neg_grants[0].reserved = 0;
    neg_grants[1].object = &neg_budget->obj;
    neg_grants[1].rights = O0_RIGHT_SPLIT | O0_RIGHT_INFO | O0_RIGHT_TRANSFER;
    neg_grants[1].reserved = 0;
    o0_thread_t *ipcneg = wave3_spawn("ipcneg",
                                      _binary_build_user_ipcneg_elf_start,
                                      _binary_build_user_ipcneg_elf_end,
                                      neg_grants, 2, 0);
    if (wave3_join(ipcneg, "ipcneg") != 0) {
        panic("IPC negative tests failed");
    }

    /* Churn: create and destroy processes, endpoints, budgets and memory
     * objects, then require the accounting to return to the baseline. */
    wave3_run_ipc_pair("warm-up");
    uint64_t pmm_before = o0_pmm_free_bytes();
    uint32_t objects_before = o0_object_live_count();
    struct o0_budget_snapshot budget_before;
    o0_budget_snapshot(o0_budget_root(), &budget_before);
    for (uint32_t i = 0; i < 8u; ++i) {
        wave3_run_ipc_pair("churn");
    }
    uint64_t pmm_after = o0_pmm_free_bytes();
    uint32_t objects_after = o0_object_live_count();
    struct o0_budget_snapshot budget_after;
    o0_budget_snapshot(o0_budget_root(), &budget_after);
    if (objects_after != objects_before || budget_after.used != budget_before.used) {
        panic("wave3 process churn leaked objects or budget");
    }
    /* The slab may retain a few pages; a leak of user pages would be far
     * larger than this allowance. */
    if (pmm_after + (8u * O0_PAGE_SIZE) < pmm_before) {
        panic("wave3 process churn leaked physical pages");
    }
    o0_serial_puts("OZERO: wave3 churn OK pairs=8\n");
    wave3_smp_stress();
    o0_serial_puts("OZERO: wave3 self-test complete\n");
}

static void wave2_demo(void) {
    o0_process_t *proc = NULL;
    if (o0_process_create("usertest", o0_as_kernel(), o0_budget_root(), &proc) != O0_OK) {
        panic("user self-test process creation failed");
    }
    struct o0_thread_attr attr = {
        .name = "usertest",
        .entry = wave2_self_test,
        .arg = NULL,
        .stack_size = 0,
        .priority = 1,
        .flags = 0,
        .user_entry = 0,
        .user_rsp = 0,
    };
    o0_thread_t *thread = NULL;
    if (o0_thread_create(proc, &attr, &thread) != O0_OK) {
        panic("user self-test thread creation failed");
    }
}

static struct o0_display_mode boot_display_mode;
static o0_paddr_t boot_framebuffer_phys;
static bool boot_framebuffer_valid;

/* Stage 6.4-6.8 desktop integration, run as a scheduled kernel thread.
 *
 * It must not run inline in kmain: the compositor loop never returns, so an
 * inline loop would keep o0_sched_start() from executing and suppress the
 * `boot success` milestone that tools/qemu-smoke.sh requires. */
static void desktop_main(void *arg) {
    (void)arg;
    static struct o0_shell desktop_shell;
    static struct o0_surface backbuffer;
    static struct o0_surface win1, win2;

    uint32_t w = (uint32_t)boot_display_mode.width;
    uint32_t h = (uint32_t)boot_display_mode.height;
    size_t pitch = (size_t)w * 4u;

    void *bb = o0_kalloc((size_t)h * pitch, 64);
    if (bb == NULL) {
        o0_serial_puts("OZERO: stage6 desktop backbuffer unavailable\n");
        return;
    }
    o0_surface_init(&backbuffer, bb, w, h, pitch, O0_PIXEL_RGBX8888);
    if (o0_shell_init(&desktop_shell, w, h) != O0_OK) {
        o0_serial_puts("OZERO: stage6 shell init failed\n");
        return;
    }

    void *p1 = o0_kalloc(200u * 4u * 150u, 64);
    void *p2 = p1 != NULL ? o0_kalloc(200u * 4u * 150u, 64) : NULL;
    if (p1 != NULL && p2 != NULL) {
        o0_surface_init(&win1, p1, 200, 150, 200 * 4, O0_PIXEL_RGBX8888);
        o0_surface_init(&win2, p2, 200, 150, 200 * 4, O0_PIXEL_RGBX8888);
        o0_surface_fill(&win1, 0xff4060a0u);
        o0_surface_fill(&win2, 0xffa04060u);
        o0_window_id id1, id2;
        o0_wm_create(&desktop_shell.windows, &win1, 10, 10, &id1);
        o0_wm_create(&desktop_shell.windows, &win2, 220, 10, &id2);
    }

    o0_serial_puts("OZERO: stage6 desktop active\n");
    for (;;) {
        o0_shell_poll_input(&desktop_shell);
        o0_compositor_compose(&backbuffer, &desktop_shell.windows, desktop_shell.background);
        if (boot_display_mode.address != NULL) {
            uint8_t *fb = (uint8_t *)boot_display_mode.address;
            for (uint32_t y = 0; y < h; y++) {
                memcpy(fb + (size_t)y * boot_display_mode.pitch,
                       (uint8_t *)bb + (size_t)y * pitch, pitch);
            }
        }
        o0_thread_sleep_ns(16000000ull);
    }
}

static bool text_starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return false;
        }
    }
    return true;
}

static const char *platform_label(const struct o0_cpu_info *cpu) {
    if (cpu == NULL || cpu->hypervisor[0] == '\0') {
        return "native";
    }
    if (text_starts_with(cpu->hypervisor, "TCG")) {
        return "qemu-tcg";
    }
    if (text_starts_with(cpu->hypervisor, "KVM")) {
        return "kvm";
    }
    if (text_starts_with(cpu->hypervisor, "HAXM")) {
        return "haxm";
    }
    if (text_starts_with(cpu->hypervisor, "Microsoft Hv")) {
        return "hyper-v";
    }
    if (text_starts_with(cpu->hypervisor, "VMware")) {
        return "vmware";
    }
    if (text_starts_with(cpu->hypervisor, "VBox")) {
        return "virtualbox";
    }
    return "other";
}

__attribute__((noreturn))
void kmain(void) {
    o0_serial_init();
    if (o0_cpu_detect() != O0_OK) {
        panic("CPU feature detection failed");
    }
    const struct o0_cpu_features *cf = o0_cpu_features();
    const struct o0_cpu_info *cpu = o0_cpu_info(0);
    if (cf == NULL || cpu == NULL) {
        panic("CPU features not available");
    }
    o0_serial_puts("\nOZERO: build commit=");
    o0_serial_puts(O0_BUILD_COMMIT);
    o0_serial_puts("\n");
    o0_serial_puts("OZERO: cpu vendor=");
    o0_serial_puts(cpu->vendor);
    o0_serial_puts(" family=");
    o0_serial_dec(cpu->family);
    o0_serial_puts(" model=");
    o0_serial_dec(cpu->model);
    o0_serial_puts(" stepping=");
    o0_serial_dec(cpu->stepping);
    o0_serial_puts(" brand=");
    o0_serial_puts(cpu->brand[0] != '\0' ? cpu->brand : "unknown");
    o0_serial_puts("\n");
    o0_serial_puts("OZERO: platform=");
    o0_serial_puts(platform_label(cpu));
    o0_serial_puts(" hypervisor=");
    o0_serial_puts(cpu->hypervisor[0] != '\0' ? cpu->hypervisor : "none");
    o0_serial_puts(" signature=");
    o0_serial_hex(cpu->hypervisor_signature);
    o0_serial_puts(" firmware=");
    o0_serial_puts(efi_system_table_request.response != NULL ? "UEFI" : "BIOS");
    o0_serial_puts("\n");
    o0_serial_puts("OZERO: kernel entry reached\n");
#ifdef O0_LOCK_ORDER_CHECK
    o0_serial_puts("OZERO: lock order check=on\n");
#else
    o0_serial_puts("OZERO: lock order check=off\n");
#endif
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

    if (rsdp_request.response != NULL && rsdp_request.response->address != NULL) {
        o0_boot_set_rsdp((o0_vaddr_t)(uintptr_t)rsdp_request.response->address);
        struct o0_acpi_info acpi;
        o0_status acpi_status =
            o0_acpi_parse(rsdp_request.response->address, o0_arch_hhdm_offset(),
                          UINT32_MAX, &acpi);
        if (acpi_status == O0_OK) {
            o0_acpi_set_info(&acpi);
            o0_serial_puts("OZERO: acpi cpus=");
            o0_serial_dec(acpi.cpu_count);
            o0_serial_puts(" ioapics=");
            o0_serial_dec(acpi.ioapic_count);
            o0_serial_puts(" overrides=");
            o0_serial_dec(acpi.override_count);
            o0_serial_puts(" lapic=");
            o0_serial_hex(acpi.lapic_address);
            o0_serial_puts(" bsp_apic=");
            o0_serial_dec(acpi.bsp_apic_id);
            o0_serial_puts("\n");
        } else {
            o0_serial_puts("OZERO: acpi unavailable status=");
            o0_serial_dec((uint64_t)(-acpi_status));
            o0_serial_puts("\n");
        }
    } else {
        o0_serial_puts("OZERO: acpi unavailable (no RSDP)\n");
    }

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

    /* Resolve the bootloader framebuffer address before replacing Limine's
     * page tables. Framebuffer-containing direct-map chunks will be UC. */
    boot_framebuffer_valid = false;
    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count != 0 &&
        framebuffer_request.response->framebuffers != NULL &&
        framebuffer_request.response->framebuffers[0] != NULL) {
        const struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
        boot_display_mode = (struct o0_display_mode) {
            .address = fb->address, .width = fb->width, .height = fb->height,
            .pitch = fb->pitch, .bytes = 0, .bpp = fb->bpp,
            .memory_model = fb->memory_model,
            .red_size = fb->red_mask_size, .red_shift = fb->red_mask_shift,
            .green_size = fb->green_mask_size, .green_shift = fb->green_mask_shift,
            .blue_size = fb->blue_mask_size, .blue_shift = fb->blue_mask_shift
        };
        if (fb->height != 0 && fb->pitch <= UINT64_MAX / fb->height) {
            boot_display_mode.bytes = fb->pitch * fb->height;
            if (o0_display_mode_validate(&boot_display_mode) == O0_OK) {
                boot_framebuffer_phys = o0_virt_to_phys(fb->address);
                boot_framebuffer_valid = boot_framebuffer_phys != 0;
            }
        }
        o0_serial_puts("OZERO: framebuffer discovery ");
        o0_serial_puts(boot_framebuffer_valid ? "valid" : "unavailable");
        o0_serial_puts("\n");
    } else {
        o0_serial_puts("OZERO: no boot framebuffer; serial fallback\n");
    }

    o0_kaslr_init();
    o0_serial_puts("OZERO: kaslr layout offset=");
    o0_serial_hex(o0_kaslr_offset());
    o0_serial_puts("\n");
    if (o0_paging_init(bi) != O0_OK) {
        panic("paging initialisation failed");
    }
    o0_serial_puts("OZERO: page tables built\n");

    if (o0_paging_load() != O0_OK) {
        panic("CR3 switch to kernel page tables failed");
    }
    o0_serial_puts("OZERO: page tables active\n");

    /* Ring-3 prerequisites: NX for W^X and SYSCALL for the native ABI. */
    if (!cf->nx) {
        panic("CPU lacks NX; W^X cannot be enforced");
    }
    if (!cf->syscall) {
        panic("CPU lacks SYSCALL; the Ozero user ABI requires it");
    }
    uint32_t smep_smap = o0_cpu_enable_smep_smap();
    o0_serial_puts("OZERO: smep=");
    o0_serial_puts((smep_smap & 1u) != 0 ? "on" : "off");
    o0_serial_puts(" smap=");
    o0_serial_puts((smep_smap & 2u) != 0 ? "on" : "off");
    o0_serial_puts("\n");

    o0_gdt_init();
    /* The early IDT used Limine's CS selector; reinstall it against our GDT. */
    x86_64_idt_init();
    x86_64_idt_install_double_fault_ist();
    o0_serial_puts("OZERO: gdt+tss installed\n");
 
    if (boot_framebuffer_valid) {
        o0_status display_status =
            o0_display_init(&boot_display_mode, boot_framebuffer_phys, bi);
        o0_serial_puts("OZERO: framebuffer presentation ");
        o0_serial_puts(display_status == O0_OK ? "active" : "rejected");
        o0_serial_puts(" physical=");
        o0_serial_hex(boot_framebuffer_phys);
        o0_serial_puts(" bytes=");
        o0_serial_dec(boot_display_mode.bytes);
        o0_serial_puts("\n");
        if (display_status == O0_OK) {
            o0_serial_puts("OZERO: framebuffer bars=8 width=");
            o0_serial_dec(boot_display_mode.width);
            o0_serial_puts(" height=");
            o0_serial_dec(boot_display_mode.height);
            o0_serial_puts("\n");
            for (uint32_t band = 0; band < 8u; ++band) {
                o0_serial_puts("OZERO: framebuffer sample bar=");
                o0_serial_dec(band);
                o0_serial_puts(" pixel=");
                o0_serial_hex(o0_display_bar_center_actual(band));
                o0_serial_puts(" expected=");
                o0_serial_hex(o0_display_bar_center_expected(&boot_display_mode, band));
                o0_serial_puts("\n");
            }
            o0_serial_puts("OZERO: framebuffer checksum=");
            o0_serial_hex(o0_display_actual_checksum());
            o0_serial_puts(" expected=");
            o0_serial_hex(o0_display_expected_checksum(&boot_display_mode));
            o0_serial_puts("\n");
        }
    } else {
        o0_serial_puts("OZERO: framebuffer unavailable; serial fallback\n");
    }

    o0_slab_init();
    o0_serial_puts("OZERO: kernel heap ready\n");
    if (o0_budget_init_system() != O0_OK) {
        panic("root budget initialisation failed");
    }
    o0_serial_puts("OZERO: root budget ready\n");

    o0_irq_dispatch_init();
    x86_64_idt_install_irq_gates();
    x86_64_idt_install_ipi_gate();
    if (o0_pic_init() != O0_OK) {
        panic("PIC initialisation failed");
    }
    o0_serial_puts("OZERO: PIC remapped to vectors 32-47\n");
    o0_serial_puts("OZERO: interrupt delivery=");
    o0_serial_puts(o0_pic_apic_extint_active() ? "lapic-extint" : "pic-legacy");
    o0_serial_puts("\n");

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

    x86_64_idt_install_apic_timer_gate();
    o0_status timer_status = o0_apic_timer_start(o0_sched_hz());
    o0_serial_puts("OZERO: timer=");
    o0_serial_puts(timer_status == O0_OK ? "lapic" : "pit");
    o0_serial_puts(" hz=");
    o0_serial_dec(o0_sched_hz());
    o0_serial_puts("\n");

    /* The I/O APIC replaces the 8259 only after the LAPIC timer is calibrated:
     * calibration uses PIT IRQ0, which the I/O APIC path masks. */
    o0_status ioapic_status = O0_ENOSYS;
    if (timer_status == O0_OK) {
        ioapic_status = o0_ioapic_init();
    }
    o0_serial_puts("OZERO: ioapic=");
    o0_serial_puts(ioapic_status == O0_OK ? "active" : "unavailable");
    o0_serial_puts(" gsis=");
    o0_serial_dec(o0_ioapic_pin_count());
    o0_serial_puts("\n");

    /* Release the APs only after the scheduler and both timer paths exist. */
    uint32_t online = o0_smp_start(mp_request.response);
    o0_serial_puts("OZERO: smp cpus=");
    o0_serial_dec(o0_cpu_count());
    o0_serial_puts(" online=");
    o0_serial_dec(online);
    o0_serial_puts("\n");
    if (o0_smp_online_count() > 1u) {
        uint64_t probe = (uint64_t)(uintptr_t)&online;
        o0_status tlb = o0_tlb_shootdown(probe);
        o0_serial_puts("OZERO: tlb shootdown ");
        o0_serial_puts(tlb == O0_OK ? "OK" : "FAILED");
        o0_serial_puts(" status=");
        o0_serial_dec((uint64_t)tlb);
        o0_serial_puts(" ipi_seen=");
        o0_serial_dec(o0_smp_ipi_seen());
        o0_serial_puts("\n");
        o0_status range = o0_tlb_shootdown_range(probe, 4u * O0_PAGE_SIZE);
        o0_serial_puts("OZERO: tlb shootdown range ");
        o0_serial_puts(range == O0_OK ? "OK" : "FAILED");
        o0_serial_puts("\n");
        o0_status full = o0_tlb_shootdown_all();
        o0_serial_puts("OZERO: tlb shootdown full ");
        o0_serial_puts(full == O0_OK ? "OK" : "FAILED");
        o0_serial_puts("\n");
    }

    uint64_t reclaimed = o0_pmm_release_bootloader_memory();
    o0_serial_puts("OZERO: bootloader reclaim bytes=");
    o0_serial_dec(reclaimed);
    o0_serial_puts("\n");

    o0_input_ps2_init();

    stage3_self_test();
    stage4_self_test();
    stage5_self_test();
    report_memory_map();
    stage3_demo();
    wave2_demo();

    /* Stage 6.4-6.8 desktop integration. The compositor loop runs as its own
     * kernel thread so the scheduler still starts and every Stage 3-6 boot
     * marker, including `boot success`, is emitted for the QEMU boot gate. */
    if (boot_framebuffer_valid) {
        o0_process_t *desktop_proc = NULL;
        if (o0_process_create("desktop", o0_as_kernel(), o0_budget_root(),
                              &desktop_proc) == O0_OK) {
            struct o0_thread_attr desktop_attr = {
                .name = "desktop",
                .entry = desktop_main,
                .arg = NULL,
                .stack_size = 0,
                .priority = 0,
                .flags = 0,
            };
            o0_thread_t *desktop_thread = NULL;
            if (o0_thread_create(desktop_proc, &desktop_attr, &desktop_thread) != O0_OK) {
                o0_serial_puts("OZERO: stage6 desktop thread creation failed\n");
            }
        }
    }

    o0_serial_puts("OZERO: boot success; starting scheduler\n");
    o0_sched_start();
}
