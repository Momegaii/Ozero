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
               $(BUILD)/o0fs.o $(BUILD)/pci.o $(BUILD)/gpu.o

HOST_CFLAGS := -std=gnu11 -O1 -Wall -Wextra -Iinclude

.PHONY: help doctor check kernel iso run run-debug test clean

help:
	@printf '%s\n' 'Ozero targets:' \
	  '  make doctor     Check host dependencies' \
	  '  make check      Check tools, build kernel/ISO and run host tests' \
	  '  make kernel     Build freestanding ELF64 kernel' \
	  '  make iso        Build BIOS/UEFI bootable ISO' \
	  '  make test       Run host-side unit tests' \
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
	$(CC) $(HOST_CFLAGS) -o $(BUILD)/pmm_test tests/pmm_test.c
	./$(BUILD)/pmm_test
	$(CC) $(HOST_CFLAGS) -o $(BUILD)/slab_test tests/slab_test.c
	./$(BUILD)/slab_test
	$(CC) $(HOST_CFLAGS) -o $(BUILD)/sched_test tests/sched_test.c
	./$(BUILD)/sched_test
	$(CC) $(HOST_CFLAGS) -o $(BUILD)/cpu_test tests/cpu_test.c
	./$(BUILD)/cpu_test
	$(CC) $(HOST_CFLAGS) -o $(BUILD)/block_test tests/block_test.c
	./$(BUILD)/block_test
	$(CC) $(HOST_CFLAGS) -o $(BUILD)/fs_test tests/fs_test.c
	./$(BUILD)/fs_test

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
