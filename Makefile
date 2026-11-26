PYTHON ?= python3
CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy
QEMU ?= qemu-system-x86_64
QEMU_CPU ?= qemu64
QEMU_SMP ?= 1
# Set to 1 to compile the kernel with the lock-order checker enabled.
LOCK_ORDER ?= 0
BUILD := build
LIMINE_COMMIT := 5be26a73d7b7b4d4477d18be94e1d16e615adf56
LIMINE_DIR := $(BUILD)/limine
LIMINE_ARCHIVE := $(BUILD)/limine-source.tar.gz
KERNEL := $(BUILD)/kernel.elf
ISO := $(BUILD)/ozero.iso

BUILD_COMMIT := $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
          -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -m64 -march=x86-64 -mtune=generic -mcmodel=kernel -mno-red-zone -mgeneral-regs-only \
          -DO0_BUILD_COMMIT=\"$(BUILD_COMMIT)\"
ifeq ($(LOCK_ORDER),1)
CFLAGS += -DO0_LOCK_ORDER_CHECK
endif
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.lds

KERNEL_OBJS := $(BUILD)/main.o $(BUILD)/serial.o $(BUILD)/string.o $(BUILD)/status.o $(BUILD)/acpi.o \
               $(BUILD)/pmm.o $(BUILD)/boot_info.o $(BUILD)/slab.o \
               $(BUILD)/sched.o $(BUILD)/thread.o $(BUILD)/sync.o \
               $(BUILD)/irq.o $(BUILD)/ioapic.o $(BUILD)/paging.o $(BUILD)/cpu.o $(BUILD)/pic.o $(BUILD)/smp.o $(BUILD)/ap_entry.o $(BUILD)/apic_timer.o $(BUILD)/kaslr.o \
               $(BUILD)/interrupts_c.o $(BUILD)/interrupts.o $(BUILD)/irq_stubs.o \
               $(BUILD)/context.o $(BUILD)/block.o $(BUILD)/ramdisk.o \
               $(BUILD)/o0fs.o $(BUILD)/pci.o $(BUILD)/dma.o $(BUILD)/virtio_blk.o $(BUILD)/ahci.o $(BUILD)/nvme.o $(BUILD)/partition.o $(BUILD)/gpu.o $(BUILD)/gpu_hsw.o \
               $(BUILD)/gpu_device.o $(BUILD)/gpu_queue.o $(BUILD)/surface.o \
               $(BUILD)/display.o $(BUILD)/input.o $(BUILD)/input_ps2.o $(BUILD)/input_router.o $(BUILD)/window.o \
               $(BUILD)/compositor.o $(BUILD)/gfx_backend.o $(BUILD)/shell.o \
               $(BUILD)/abi.o $(BUILD)/elf.o $(BUILD)/usercopy.o $(BUILD)/usercopy_stub.o \
               $(BUILD)/user.o $(BUILD)/syscall_c.o $(BUILD)/syscall_entry.o \
               $(BUILD)/gdt.o \
               $(BUILD)/object.o $(BUILD)/budget.o $(BUILD)/handle.o \
               $(BUILD)/memory.o $(BUILD)/endpoint.o $(BUILD)/ipc.o

# Static user images, embedded in the kernel image for the Wave 2 self-tests.
USER_PROGS := init quiet fault_null fault_priv fault_kjump badptr \
              spawner spawn_child ipc_server ipc_client handles budget ipcneg peer_server
USER_ELFS := $(addprefix $(BUILD)/user_,$(addsuffix .elf,$(USER_PROGS)))
USER_BLOBS := $(addprefix $(BUILD)/blob_,$(addsuffix .o,$(USER_PROGS)))
KERNEL_OBJS += $(USER_BLOBS)

HOST_CFLAGS := -std=gnu11 -O1 -Wall -Wextra -Iinclude -DO0_LOCK_ORDER_CHECK
# Extra flags and per-test environment let the same recipes run under sanitizers
# (see test-sanitize) without duplicating every test invocation.
HOST_EXTRA_CFLAGS ?=
HOST_RUN_ENV ?=
SAN_CFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g
SAN_ENV := ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

.PHONY: help doctor check kernel iso run run-debug test test-sanitize clean framebuffer-check

help:
	@printf '%s\n' 'Ozero targets:' \
	  '  make doctor     Check host dependencies' \
	  '  make check      Check tools, build kernel/ISO and run host tests' \
	  '  make kernel     Build freestanding ELF64 kernel' \
	  '  make iso        Build BIOS/UEFI bootable ISO' \
	  '  make test       Run host-side unit tests' \
	  '  make test-sanitize  Run host tests under ASan/UBSan' \
	  '  make run        Boot ISO in QEMU with serial console' \
	  '  make run-debug  Start QEMU paused with GDB on :1234' \
	  '  make clean      Remove generated artifacts'

doctor:
	$(PYTHON) tools/doctor.py

check: doctor iso test
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(MAKE) framebuffer-check; \
	else \
		echo "SKIP: framebuffer check ($(QEMU) not installed)"; \
	fi

framebuffer-check: $(ISO)
	$(PYTHON) tools/framebuffer-check.py --iso $(ISO) \
		--serial $(BUILD)/qemu-serial-framebuffer.log \
		--screendump $(BUILD)/qemu-framebuffer.ppm

kernel: $(KERNEL)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: kernel/src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -Ithird_party/limine -Iinclude -Ikernel -Ikernel/src -c $< -o $@

$(BUILD)/paging.o: kernel/arch/x86_64/paging.c | $(BUILD)
	$(CC) $(CFLAGS) -Ithird_party/limine -Iinclude -c $< -o $@

$(BUILD)/cpu.o: kernel/arch/x86_64/cpu.c | $(BUILD)
	$(CC) $(CFLAGS) -Ithird_party/limine -Iinclude -c $< -o $@

$(BUILD)/pic.o: kernel/arch/x86_64/pic.c | $(BUILD)
	$(CC) $(CFLAGS) -Ithird_party/limine -Iinclude -c $< -o $@

$(BUILD)/interrupts_c.o: kernel/arch/x86_64/interrupts.c | $(BUILD)
	$(CC) $(CFLAGS) -Ithird_party/limine -Iinclude -c $< -o $@

$(BUILD)/gdt.o: kernel/arch/x86_64/gdt.c | $(BUILD)
	$(CC) $(CFLAGS) -Ithird_party/limine -Iinclude -c $< -o $@

$(BUILD)/syscall_c.o: kernel/src/syscall.c | $(BUILD)
	$(CC) $(CFLAGS) -Ithird_party/limine -Iinclude -Ikernel -Ikernel/src -c $< -o $@

$(BUILD)/syscall_entry.o: kernel/arch/x86_64/syscall.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(BUILD)/usercopy_stub.o: kernel/arch/x86_64/usercopy.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(BUILD)/ap_entry.o: kernel/arch/x86_64/ap_entry.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

# Static user images, then their embedded blob objects.
$(BUILD)/user_%.o: user/%.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(BUILD)/user_%.elf: $(BUILD)/user_%.o user/user.ld
	$(LD) -nostdlib -static -z max-page-size=0x1000 -T user/user.ld -o $@ $<

$(BUILD)/blob_%.o: $(BUILD)/user_%.elf
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@


$(BUILD)/interrupts.o: kernel/arch/x86_64/interrupts.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(BUILD)/irq_stubs.o: kernel/arch/x86_64/irq.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(BUILD)/context.o: kernel/arch/x86_64/context.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(KERNEL): $(KERNEL_OBJS) kernel/linker.lds
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)
	@readelf -h $@ | grep -E 'Class:|Machine:|Entry point'

$(LIMINE_ARCHIVE): | $(BUILD)
	curl --fail --location --silent --show-error \
	  https://github.com/Limine-Bootloader/Limine/archive/$(LIMINE_COMMIT).tar.gz \
	  --output $@

$(LIMINE_DIR)/limine.c: $(LIMINE_ARCHIVE)
	mkdir -p $(LIMINE_DIR)
	tar -xzf $(LIMINE_ARCHIVE) --strip-components=1 -C $(LIMINE_DIR)
	test -f $@

$(LIMINE_DIR)/limine: $(LIMINE_DIR)/limine.c
	$(MAKE) -C $(LIMINE_DIR)

iso: $(ISO)

$(ISO): $(KERNEL) $(LIMINE_DIR)/limine
	rm -rf $(BUILD)/iso_root
	mkdir -p $(BUILD)/iso_root/boot/limine $(BUILD)/iso_root/EFI/BOOT
	cp $(KERNEL) $(BUILD)/iso_root/boot/kernel.elf
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin $(BUILD)/iso_root/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(BUILD)/iso_root/EFI/BOOT/
	cp boot/limine.conf $(BUILD)/iso_root/boot/limine/limine.conf
	xorriso -as mkisofs -R -r -J \
	  -b boot/limine/limine-bios-cd.bin -no-emul-boot \
	  -boot-load-size 4 -boot-info-table \
	  --efi-boot boot/limine/limine-uefi-cd.bin \
	  -efi-boot-part --efi-boot-image --protective-msdos-label \
	  $(BUILD)/iso_root -o $@
	$(LIMINE_DIR)/limine bios-install $@

# Host-side unit tests: the allocator and the scheduler queue are pure C and
# run on the build host; the kernel image itself is never executed here.
test: $(BUILD)
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/pmm_test tests/pmm_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/pmm_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -o $(BUILD)/acpi_test tests/acpi_test.c kernel/src/acpi.c
	$(HOST_RUN_ENV) ./$(BUILD)/acpi_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -o $(BUILD)/ioapic_test tests/ioapic_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/ioapic_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -Ikernel/src -o $(BUILD)/lock_order_test tests/lock_order_test.c kernel/src/sync.c
	$(HOST_RUN_ENV) ./$(BUILD)/lock_order_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -o $(BUILD)/kaslr_test tests/kaslr_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/kaslr_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/slab_test tests/slab_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/slab_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/sched_test tests/sched_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/sched_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/cpu_test tests/cpu_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/cpu_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/abi_test tests/abi_test.c kernel/src/abi.c
	$(HOST_RUN_ENV) ./$(BUILD)/abi_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/elf_test tests/elf_test.c kernel/src/elf.c
	$(HOST_RUN_ENV) ./$(BUILD)/elf_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Ikernel/src -o $(BUILD)/usercopy_test tests/usercopy_test.c kernel/src/usercopy.c
	$(HOST_RUN_ENV) ./$(BUILD)/usercopy_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Ikernel/src -o $(BUILD)/cap_test tests/cap_test.c kernel/src/object.c kernel/src/budget.c kernel/src/handle.c kernel/src/memory.c kernel/src/endpoint.c
	$(HOST_RUN_ENV) ./$(BUILD)/cap_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/block_test tests/block_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/block_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Ikernel/src -o $(BUILD)/partition_test tests/partition_test.c kernel/src/partition.c kernel/src/block.c kernel/src/ramdisk.c
	$(HOST_RUN_ENV) ./$(BUILD)/partition_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/fs_test tests/fs_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/fs_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -o $(BUILD)/storage_encode_test tests/storage_encode_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/storage_encode_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -o $(BUILD)/virtio_ring_test tests/virtio_ring_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/virtio_ring_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Ikernel/src -o $(BUILD)/pci_test tests/pci_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/pci_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Ikernel/src -o $(BUILD)/dma_test tests/dma_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/dma_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Ikernel/src -o $(BUILD)/gpu_test tests/gpu_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/gpu_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Ikernel -Ikernel/src -o $(BUILD)/display_test tests/display_test.c kernel/src/display.c kernel/arch/x86_64/paging.c
	$(HOST_RUN_ENV) ./$(BUILD)/display_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/input_test tests/input_test.c kernel/src/input.c
	$(HOST_RUN_ENV) ./$(BUILD)/input_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/surface_test tests/surface_test.c kernel/src/surface.c
	$(HOST_RUN_ENV) ./$(BUILD)/surface_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Ikernel -Ikernel/src -Ikernel/arch/x86_64 -o $(BUILD)/input_ps2_test tests/input_ps2_test.c kernel/src/input_ps2.c kernel/src/input.c
	$(HOST_RUN_ENV) ./$(BUILD)/input_ps2_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -o $(BUILD)/window_test tests/window_test.c kernel/src/window.c kernel/src/surface.c kernel/src/status.c
	$(HOST_RUN_ENV) ./$(BUILD)/window_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -o $(BUILD)/compositor_test tests/compositor_test.c kernel/src/compositor.c kernel/src/window.c kernel/src/surface.c kernel/src/status.c
	$(HOST_RUN_ENV) ./$(BUILD)/compositor_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -o $(BUILD)/gfx_backend_test tests/gfx_backend_test.c kernel/src/gfx_backend.c kernel/src/surface.c kernel/src/status.c
	$(HOST_RUN_ENV) ./$(BUILD)/gfx_backend_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Iinclude -o $(BUILD)/shell_test tests/shell_test.c kernel/src/shell.c kernel/src/input_router.c kernel/src/window.c kernel/src/surface.c kernel/src/status.c kernel/src/input.c
	$(HOST_RUN_ENV) ./$(BUILD)/shell_test

# Same suites, compiled and run under AddressSanitizer and
# UndefinedBehaviorSanitizer. Leak detection is on: a test binary that leaks is a
# failure, not a warning.
test-sanitize: $(BUILD)
	rm -f $(BUILD)/pmm_test $(BUILD)/slab_test $(BUILD)/sched_test \
	      $(BUILD)/cpu_test $(BUILD)/abi_test $(BUILD)/elf_test $(BUILD)/acpi_test $(BUILD)/ioapic_test $(BUILD)/lock_order_test $(BUILD)/kaslr_test $(BUILD)/cap_test \
	      $(BUILD)/block_test $(BUILD)/partition_test $(BUILD)/fs_test $(BUILD)/storage_encode_test \
	      $(BUILD)/pci_test $(BUILD)/dma_test $(BUILD)/gpu_test $(BUILD)/display_test \
	      $(BUILD)/input_test $(BUILD)/surface_test $(BUILD)/input_ps2_test \
	      $(BUILD)/window_test $(BUILD)/compositor_test $(BUILD)/gfx_backend_test $(BUILD)/shell_test
	$(MAKE) test HOST_EXTRA_CFLAGS="$(SAN_CFLAGS)" HOST_RUN_ENV="$(SAN_ENV)"

run: $(ISO)
	$(QEMU) -machine q35 -cpu $(QEMU_CPU) -smp $(QEMU_SMP) -m 512M -cdrom $(ISO) -boot d \
	  -display none -serial stdio -monitor none -no-reboot -no-shutdown

run-debug: $(ISO)
	$(QEMU) -machine q35 -cpu $(QEMU_CPU) -smp $(QEMU_SMP) -m 512M -cdrom $(ISO) -boot d \
	  -display none -serial stdio -monitor none -no-reboot -no-shutdown \
	  -s -S

clean:
	rm -rf $(BUILD)
	find tools -type d -name __pycache__ -prune -exec rm -rf {} +
