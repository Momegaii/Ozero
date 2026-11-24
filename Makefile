PYTHON ?= python3
CC ?= gcc
LD ?= ld
QEMU ?= qemu-system-x86_64
BUILD := build
LIMINE_COMMIT := 5be26a73d7b7b4d4477d18be94e1d16e615adf56
LIMINE_DIR := $(BUILD)/limine
LIMINE_ARCHIVE := $(BUILD)/limine-source.tar.gz
KERNEL := $(BUILD)/kernel.elf
ISO := $(BUILD)/ozero.iso

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
          -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -m64 -march=x86-64 -mtune=generic -mcmodel=kernel -mno-red-zone -mgeneral-regs-only
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.lds

KERNEL_OBJS := $(BUILD)/main.o $(BUILD)/serial.o $(BUILD)/string.o $(BUILD)/status.o \
               $(BUILD)/pmm.o $(BUILD)/boot_info.o $(BUILD)/slab.o \
               $(BUILD)/sched.o $(BUILD)/thread.o $(BUILD)/sync.o \
               $(BUILD)/irq.o $(BUILD)/paging.o $(BUILD)/cpu.o $(BUILD)/pic.o \
               $(BUILD)/interrupts_c.o $(BUILD)/interrupts.o $(BUILD)/irq_stubs.o \
               $(BUILD)/context.o $(BUILD)/block.o $(BUILD)/ramdisk.o \
               $(BUILD)/o0fs.o $(BUILD)/pci.o $(BUILD)/gpu.o $(BUILD)/gpu_hsw.o \
               $(BUILD)/gpu_device.o $(BUILD)/gpu_queue.o $(BUILD)/surface.o \
               $(BUILD)/display.o $(BUILD)/input.o $(BUILD)/input_ps2.o $(BUILD)/window.o \
               $(BUILD)/compositor.o $(BUILD)/gfx_backend.o $(BUILD)/shell.o

HOST_CFLAGS := -std=gnu11 -O1 -Wall -Wextra -Iinclude
# Extra flags and per-test environment let the same recipes run under sanitizers
# (see test-sanitize) without duplicating every test invocation.
HOST_EXTRA_CFLAGS ?=
HOST_RUN_ENV ?=
SAN_CFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g
SAN_ENV := ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

.PHONY: help doctor check kernel iso run run-debug test test-sanitize clean

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

$(BUILD)/interrupts.o: kernel/arch/x86_64/interrupts.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(BUILD)/irq_stubs.o: kernel/arch/x86_64/irq.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(BUILD)/context.o: kernel/arch/x86_64/context.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(KERNEL): $(KERNEL_OBJS) kernel/linker.lds
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)
	@file $@

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
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/slab_test tests/slab_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/slab_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/sched_test tests/sched_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/sched_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/cpu_test tests/cpu_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/cpu_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/block_test tests/block_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/block_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -o $(BUILD)/fs_test tests/fs_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/fs_test
	$(CC) $(HOST_CFLAGS) $(HOST_EXTRA_CFLAGS) -Ikernel/src -o $(BUILD)/pci_test tests/pci_test.c
	$(HOST_RUN_ENV) ./$(BUILD)/pci_test
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

# Same suites, compiled and run under AddressSanitizer and
# UndefinedBehaviorSanitizer. Leak detection is on: a test binary that leaks is a
# failure, not a warning.
test-sanitize: $(BUILD)
	rm -f $(BUILD)/pmm_test $(BUILD)/slab_test $(BUILD)/sched_test \
	      $(BUILD)/cpu_test $(BUILD)/block_test $(BUILD)/fs_test \
	      $(BUILD)/pci_test $(BUILD)/gpu_test $(BUILD)/display_test \
	      $(BUILD)/input_test $(BUILD)/surface_test $(BUILD)/input_ps2_test
	$(MAKE) test HOST_EXTRA_CFLAGS="$(SAN_CFLAGS)" HOST_RUN_ENV="$(SAN_ENV)"

run: $(ISO)
	$(QEMU) -machine q35 -m 512M -cdrom $(ISO) -boot d \
	  -display none -serial stdio -monitor none -no-reboot -no-shutdown

run-debug: $(ISO)
	$(QEMU) -machine q35 -m 512M -cdrom $(ISO) -boot d \
	  -display none -serial stdio -monitor none -no-reboot -no-shutdown \
	  -s -S

clean:
	rm -rf $(BUILD)
	find tools -type d -name __pycache__ -prune -exec rm -rf {} +
