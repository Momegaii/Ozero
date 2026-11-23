SHELL := /bin/bash
.DEFAULT_GOAL := ci

BUILD_DIR ?= build
export BUILD_DIR

.PHONY: all build test test-mbr-harness targets ci check-toolchain check-ci-safety verify-repro verify-clean clean help

all: build

build:
	@scripts/build.sh

test: test-mbr-harness

test-mbr-harness: build
	@scripts/test-qemu.sh

targets:
	@scripts/list-targets.sh

ci: check-toolchain check-ci-safety build test
	@echo "ci: ok"

check-toolchain:
	@scripts/check-toolchain.sh

check-ci-safety:
	@scripts/check-ci-safety.sh

verify-repro:
	@scripts/verify-reproducible.sh

verify-clean:
	@scripts/verify-clean-checkout.sh

clean:
	@rm -rf "$(BUILD_DIR)"
	@echo "clean: removed $(BUILD_DIR)"

help:
	@echo "Targets:"
	@echo "  build              build the Stage 0 MBR fixture into $(BUILD_DIR)/"
	@echo "  test-mbr-harness   boot the MBR fixture in QEMU and check serial markers"
	@echo "  test               alias for test-mbr-harness (not kernel boot)"
	@echo "  targets            list test targets and their status"
	@echo "  ci                 check-toolchain + check-ci-safety + build + test"
	@echo "  check-ci-safety    reject privileged, raw-disk or Windows-binary CI steps"
	@echo "  verify-repro       prove two clean builds are bit-identical (needs a commit)"
	@echo "  verify-clean       clone HEAD to a temp dir and run ci there"
	@echo "  clean              remove $(BUILD_DIR)/"
