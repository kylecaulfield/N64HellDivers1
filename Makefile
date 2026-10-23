# HIVEBREAKERS - top-level build
#
#   make              build the ROM -> hivebreakers.z64
#   make test         build and run the host unit tests (no N64 toolchain needed)
#   make AUTOTEST=1   scripted-input smoke-test ROM -> hivebreakers-autotest.z64
#   make toolchain    one-time: build the MIPS toolchain from source (30-60 min)
#   make clean        remove game build output (keeps the built SDK)
#   make distclean    also remove the built libdragon/Tiny3D SDK
#
# The toolchain lives in .toolchain/inst by default. To use a toolchain you
# already have, pass N64_GCCPREFIX=/path/to/it (the dir containing bin/mips64-elf-gcc).
#
# libdragon and Tiny3D are built from the pinned submodules and installed into
# build/sdk, which is what the ROM build uses as N64_INST. Changing a submodule
# pin rebuilds the SDK automatically.

PROJECT       := hivebreakers
ROOT          := $(CURDIR)
N64_GCCPREFIX ?= $(ROOT)/.toolchain/inst
JOBS          ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Pin-derived stamps: a new submodule commit means a new stamp, so a rebuild.
LIBDRAGON_REV := $(shell git -C vendor/libdragon rev-parse --short=12 HEAD 2>/dev/null || echo local)
TINY3D_REV    := $(shell git -C vendor/tiny3d rev-parse --short=12 HEAD 2>/dev/null || echo local)

LIBDRAGON_DIR := vendor/libdragon
TINY3D_DIR    := vendor/tiny3d

SDK             := $(ROOT)/build/sdk
LIBDRAGON_STAMP := $(SDK)/.libdragon-$(LIBDRAGON_REV)
TINY3D_STAMP    := $(SDK)/.tiny3d-$(TINY3D_REV)
SDK_VARS        := N64_INST=$(SDK) N64_GCCPREFIX=$(N64_GCCPREFIX)

.PHONY: all rom sdk test toolchain clean distclean check-toolchain check-sim-purity

all: rom

# AUTOTEST=1 builds hivebreakers-autotest.z64: controller 1 is replaced by a
# scripted bot that exercises every feature and logs AUTOTEST PASS/FAIL.
ifeq ($(AUTOTEST),1)
ROM_VARS := PROJECT=$(PROJECT)-autotest BUILD_DIR=build/rom-autotest AUTOTEST=1
endif

rom: sdk
	$(MAKE) -f rom.mk $(SDK_VARS) $(ROM_VARS) -j$(JOBS)

sdk: $(LIBDRAGON_STAMP) $(TINY3D_STAMP)

check-toolchain:
	@if [ ! -x "$(N64_GCCPREFIX)/bin/mips64-elf-gcc" ]; then \
		echo "No N64 toolchain at $(N64_GCCPREFIX)."; \
		echo "Run 'make toolchain' once, or pass N64_GCCPREFIX=/path/to/toolchain."; \
		exit 1; \
	fi
	@if [ ! -f vendor/libdragon/n64.mk ] || [ ! -f vendor/tiny3d/t3d.mk ]; then \
		echo "Submodules missing. Run: git submodule update --init --recursive"; \
		exit 1; \
	fi

$(LIBDRAGON_STAMP): | check-toolchain
	rm -f $(SDK)/.libdragon-*
	$(MAKE) -C $(LIBDRAGON_DIR) $(SDK_VARS) -j$(JOBS) install
	$(MAKE) -C $(LIBDRAGON_DIR) $(SDK_VARS) -j$(JOBS) tools-install
	touch $@

# Tiny3D's library build includes the installed n64.mk, so libdragon goes
# first.
#
# Tiny3D builds with -Wextra -Wformat-signedness -Werror, which trips on
# warnings inside libdragon's own headers at our pin (magma.h, rdpq.h). Those
# headers are third-party code, so Tiny3D's build sees them as system headers
# (-isystem makes GCC ignore the matching -I); Tiny3D's own code is still
# built with -Werror. Passed via the environment so Tiny3D's own CFLAGS
# additions still apply.
$(TINY3D_STAMP): $(LIBDRAGON_STAMP)
	rm -f $(SDK)/.tiny3d-*
	CFLAGS="-isystem $(SDK)/mips64-elf/include" $(MAKE) -C $(TINY3D_DIR) $(SDK_VARS) -j$(JOBS) install
	$(MAKE) -C vendor/tiny3d/tools/gltf_importer $(SDK_VARS) -j$(JOBS)
	$(MAKE) -C vendor/tiny3d/tools/gltf_importer $(SDK_VARS) install
	touch $@

toolchain:
	N64_INST=$(N64_GCCPREFIX) tools/setup-toolchain.sh

# ---------------------------------------------------------------------------
# Host unit tests for the pure-C sim core. Built with the host compiler and
# with no N64 include paths, so any libdragon dependency in src/sim fails here.

HOST_CC     ?= cc
TEST_DIR    := build/test
SIM_SRC     := $(wildcard src/sim/*.c)
TEST_SRC    := $(wildcard tests/*.c)
# Set SANITIZE= (empty) if your compiler lacks the sanitizer runtimes.
SANITIZE    ?= address,undefined
TEST_CFLAGS := -std=gnu17 -O1 -g -Wall -Wextra -Werror -Wshadow -Wdouble-promotion \
               -ffp-contract=off -fno-omit-frame-pointer \
               $(if $(SANITIZE),-fsanitize=$(SANITIZE) -fno-sanitize-recover=all) \
               -Isrc/sim -Itests -I$(TEST_DIR)

$(TEST_DIR)/test_list.h: $(filter-out tests/test_main.c,$(TEST_SRC))
	@mkdir -p $(dir $@)
	@grep -hoE '^void test_[A-Za-z0-9_]+\(void\)' $^ | sed -E 's/^void (test_[A-Za-z0-9_]+)\(void\)/X(\1)/' > $@

$(TEST_DIR)/run_tests: $(SIM_SRC) $(TEST_SRC) $(wildcard src/sim/*.h) tests/test.h $(TEST_DIR)/test_list.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(TEST_CFLAGS) -o $@ $(SIM_SRC) $(TEST_SRC) -lm

check-sim-purity:
	@if grep -rnE '#[[:space:]]*include[[:space:]]*[<"](libdragon|t3d/|rdpq|rspq|display|joypad|n64sys|debug)' src/sim; then \
		echo "src/sim must not include libdragon or Tiny3D headers"; exit 1; \
	fi

test: check-sim-purity $(TEST_DIR)/run_tests
	$(TEST_DIR)/run_tests

# ---------------------------------------------------------------------------

clean:
	rm -rf build/rom build/rom-autotest build/filesystem build/gen build/test \
	       $(PROJECT).z64 $(PROJECT)-autotest.z64

distclean: clean
	rm -rf build
	-$(MAKE) -C vendor/libdragon clean
	-$(MAKE) -C vendor/libdragon tools-clean
	-$(MAKE) -C vendor/tiny3d/tools/gltf_importer clean
	rm -rf vendor/tiny3d/build
