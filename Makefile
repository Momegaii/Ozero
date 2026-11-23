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
          -m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.lds

.PHONY: help doctor check kernel iso run run-debug clean

help:
	@printf '%s\\n' 'Ozero targets:' \
	  '  make doctor     Check host dependencies' \
	  '  make check      Build kernel and bootable ISO' \
	  '  make kernel     Build freestanding ELF64 kernel' \
	  '  make iso        Build BIOS/UEFI bootable ISO' \
	  '  make run        Boot ISO in QEMU with serial console' \
	  '  make run-debug  Start QEMU paused with GDB on :1234' \
	  '  make clean      Remove generated artifacts'

doctor:
	$(PYTHON) tools/doctor.py

check: doctor iso

kernel: $(KERNEL)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: kernel/src/main.c third_party/limine/limine.h | $(BUILD)
	$(CC) $(CFLAGS) -Ithird_party/limine -c $< -o $@

$(BUILD)/interrupts.o: kernel/arch/x86_64/interrupts.S | $(BUILD)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -c $< -o $@

$(BUILD)/interrupts_c.o: kernel/arch/x86_64/interrupts.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(BUILD)/main.o $(BUILD)/interrupts.o $(BUILD)/interrupts_c.o kernel/linker.lds
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/main.o $(BUILD)/interrupts.o $(BUILD)/interrupts_c.o
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
