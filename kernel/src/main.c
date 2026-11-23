#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#define COM1 0x3f8

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start"), aligned(8)))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"), aligned(8)))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}

static void serial_putc(char c) {
    if (c == '\n') {
        outb(COM1, '\r');
    }
    outb(COM1, (uint8_t)c);
}

static void serial_puts(const char *text) {
    while (*text != '\0') {
        serial_putc(*text++);
    }
}

static void serial_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    serial_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(digits[(value >> (unsigned)shift) & 0xfU]);
    }
}

static void serial_dec(uint64_t value) {
    char buffer[21];
    size_t count = 0;
    do {
        buffer[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0 && count < sizeof(buffer));
    while (count > 0) {
        serial_putc(buffer[--count]);
    }
}

__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt" ::: "memory");
    }
}

__attribute__((noreturn))
static void panic(const char *reason) {
    serial_puts("OZERO: PANIC: ");
    serial_puts(reason);
    serial_puts("\n");
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

void x86_exception_dispatch(struct x86_64_exception_frame *frame) {
    serial_puts("\nOZERO: EXCEPTION vector=");
    serial_dec(frame->vector);
    serial_puts(" error=");
    serial_hex(frame->error_code);
    serial_puts(" rip=");
    serial_hex(frame->rip);
    if (frame->vector == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        serial_puts(" cr2=");
        serial_hex(cr2);
    }
    serial_puts("\n");
    halt_forever();
}

static void report_memory_map(void) {
    struct limine_memmap_response *response = memmap_request.response;
    if (response == NULL || response->entries == NULL) {
        panic("Limine did not provide a memory map");
    }

    serial_puts("OZERO: memory map entries=");
    serial_dec(response->entry_count);
    serial_puts("\n");

    for (uint64_t i = 0; i < response->entry_count; ++i) {
        const struct limine_memmap_entry *entry = response->entries[i];
        if (entry == NULL) {
            panic("memory map contains a null entry");
        }
        serial_puts("  [");
        serial_dec(i);
        serial_puts("] base=");
        serial_hex(entry->base);
        serial_puts(" length=");
        serial_hex(entry->length);
        serial_puts(" type=");
        serial_puts(memmap_type(entry->type));
        serial_puts("\n");
    }
}

__attribute__((noreturn))
void kmain(void) {
    serial_init();
    serial_puts("\nOZERO: kernel entry reached\n");
    serial_puts("OZERO: architecture=x86_64\n");
    serial_puts("OZERO: boot protocol=Limine\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        panic("bootloader does not support requested Limine base revision 6");
    }

    serial_puts("OZERO: Limine base revision accepted\n");
    x86_64_idt_init();
    serial_puts("OZERO: IDT installed (CPU exceptions 0-31)\n");
    report_memory_map();
    serial_puts("OZERO: boot success; entering idle halt loop\n");
    halt_forever();
}
