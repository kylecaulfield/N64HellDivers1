# ROM build. Invoked by the top-level Makefile with N64_INST pointing at the
# SDK built from the pinned submodules (build/sdk). Not meant to be run directly.

PROJECT   ?= hivebreakers
BUILD_DIR ?= build/rom
SOURCE_DIR = src
FS_DIR    = build/filesystem
GEN_DIR   = build/gen

LIBDRAGON_PREVIEW = 2
N64_MKDFS_ROOT = $(FS_DIR)

include $(N64_INST)/include/n64.mk
include $(N64_INST)/include/t3d.mk

N64_ROM_TITLE = "HIVEBREAKERS"
N64_ROM_EXPANSIONPAK = required
N64_ROM_SAVETYPE = none
N64_ROM_REGIONFREE = 1

# libdragon/Tiny3D headers are third-party: -isystem keeps our -Wextra from
# failing the build on warnings inside them (GCC drops the matching -I).
N64_CFLAGS += -std=gnu17 -Wall -Wextra -Werror -Wshadow -Wdouble-promotion \
              -ffp-contract=off -isystem $(N64_INCLUDEDIR) \
              -Isrc/sim -Isrc/n64 -I$(BUILD_DIR)/gen

ifeq ($(AUTOTEST),1)
N64_CFLAGS += -DAUTOTEST
endif
ifeq ($(BENCH),1)
N64_CFLAGS += -DBENCH_AT_BOOT
endif

SRC := $(wildcard src/sim/*.c) $(wildcard src/n64/*.c)
OBJ := $(SRC:src/%.c=$(BUILD_DIR)/%.o)

# --- Models -----------------------------------------------------------------
# Every model has a generated placeholder. A file in assets/models/<name>.glb
# (e.g. a Blender/Fast64 export) replaces it with no other changes.
MODELS := soldier skitter gnasher bulwark dropship beacon turret crate mgcase pod rock_a rock_b rock_c ground
model_src = $(if $(wildcard assets/models/$(1).glb),assets/models/$(1).glb,$(GEN_DIR)/$(1).glb)
MODEL_T3DM := $(MODELS:%=$(FS_DIR)/models/%.t3dm)

$(GEN_DIR)/%.glb: tools/genmodels.py
	@mkdir -p $(dir $@)
	python3 tools/genmodels.py $(GEN_DIR) $*
	@touch $@

.SECONDEXPANSION:
$(FS_DIR)/models/%.t3dm: $$(call model_src,$$*)
	@mkdir -p $(dir $@)
	@echo "    [T3D-MODEL] $@ <- $<"
	$(T3D_GLTF_TO_3D) "$<" $@ --asset-path=assets
	$(N64_BINDIR)/mkasset -c 1 -o $(dir $@) $@

# --- Version stamp (rewritten only when it changes) --------------------------
VERSION := $(shell git describe --always --dirty --abbrev=8 2>/dev/null || echo unknown)
BUILD_DATE := $(shell date -u +%Y-%m-%d)

$(BUILD_DIR)/gen/build_info.h: FORCE
	@mkdir -p $(dir $@)
	@printf '#define GAME_VERSION "%s"\n#define GAME_BUILD_DATE "%s"\n' "$(VERSION)" "$(BUILD_DATE)" > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

$(OBJ): $(BUILD_DIR)/gen/build_info.h

# --- ROM --------------------------------------------------------------------
all: $(PROJECT).z64

$(BUILD_DIR)/$(PROJECT).dfs: $(MODEL_T3DM)
$(BUILD_DIR)/$(PROJECT).elf: $(OBJ)
$(PROJECT).z64: $(BUILD_DIR)/$(PROJECT).dfs

FORCE:
.PHONY: all FORCE
.DEFAULT_GOAL := all

-include $(wildcard $(BUILD_DIR)/*/*.d)
