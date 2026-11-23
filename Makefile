SHELL := /bin/bash
.DEFAULT_GOAL := ci

BUILD_DIR ?= build
export BUILD_DIR

.PHONY: all build test ci check-toolchain verify-repro verify-clean clean help

all: build

build:
	@scripts/build.sh

test: build
	@scripts/test-qemu.sh

ci: check-toolchain build test
	@echo "ci: ok"

check-toolchain:
	@scripts/check-toolchain.sh

verify-repro:
	@scripts/verify-reproducible.sh

verify-clean:
	@scripts/verify-clean-checkout.sh

clean:
	@rm -rf "$(BUILD_DIR)"
	@echo "clean: removed $(BUILD_DIR)"

help:
	@echo "Targets:"
	@echo "  build          build the Stage 0 boot fixture into $(BUILD_DIR)/"
	@echo "  test           boot it in QEMU and check serial markers"
	@echo "  ci             check-toolchain + build + test"
	@echo "  verify-repro   prove two clean builds are bit-identical (needs a commit)"
	@echo "  verify-clean   clone HEAD to a temp dir and run ci there"
	@echo "  clean          remove $(BUILD_DIR)/"
