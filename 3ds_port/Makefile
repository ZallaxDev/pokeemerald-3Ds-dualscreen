.SUFFIXES:
# The game compile is a preprocessor pipeline; without pipefail a failing first
# stage leaves a truncated object behind and only the link notices.
SHELL := bash
.SHELLFLAGS := -o pipefail -c
.DEFAULT_GOAL := all

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
CTRULIB := $(DEVKITPRO)/libctru
CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CPPFLAGS := -DPORTABLE -DPLATFORM_3DS -D__3DS__ -Iinclude -I$(CTRULIB)/include
CFLAGS := $(ARCH) -O2 -g -std=gnu99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections
LDFLAGS := $(ARCH) -L$(CTRULIB)/lib
# Observe the public GX queue for upload admission; never start it early.
LDFLAGS += -Wl,--wrap=GX_BindQueue
# The GPU started early in a frame (3ds_video.c GpuStartEarly) flushes first.
LDFLAGS += -Wl,--wrap=GX_ProcessCommandList -Wl,--wrap=GX_TextureCopy -Wl,--wrap=GX_DisplayTransfer -Wl,--wrap=GX_RequestDma
# Cache flushes made by the kernel call itself, not through the GSP service (3ds_video.c).
LDFLAGS += -Wl,--wrap=GSPGPU_FlushDataCache
LIBS := -lctru -lm
TARGET := emerald3ds
APP_TITLE := Pokemon Emerald 3Ds Dual Screen
# File names of the release executable (no spaces).
APP_FILE := Emerald3DS
APP_DESCRIPTION := Dual-screen port for Nintendo 3DS
APP_AUTHOR := ZallaxDev
# The release this build is, shown under the title screen (3ds_bottom_ui.c);
# tools/build_release.py refuses a --version that differs.
APP_VERSION := 0.3.0
# 1: this 3DSX carries its game data in its own RomFS (development builds).
# The release target always builds an executable without it.
EMBED_DATA ?= 1
# 0 chooses at runtime (loose files, then embedded data, then the data pack);
# 1 = RomFS, 2 = loose files, 3 = data pack.
DATA_BACKEND ?= 0
PYTHON ?= python
HOSTCC ?= gcc
ROOT := ..
SCRIPTS := ../tools/port_common
EXESUFFIX := $(if $(filter Windows_NT,$(OS)),.exe,)
SOURCES := src/main_3ds.c src/3ds_log.c src/3ds_platform.c src/3ds_input.c src/3ds_input_decode.c src/3ds_fs.c src/3ds_audio.c
SOURCES += src/3ds_data.c src/3ds_pak.c src/3ds_settings.c src/3ds_prof.c
SOURCES += src/3ds_video.c src/3ds_video_decode.c src/3ds_bottom_screen.c
LIBS := -lcitro2d -lcitro3d $(LIBS)
OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))
GAMEFLAGS := -DPORTABLE -DPLATFORM_3DS -D__3DS__ -DMODERN=1 -DUBFIX -D__INTELLISENSE__
GAMEFLAGS += -iquote include -iquote ../include -iquote ../include/constants -iquote ..
# VOXEL=0 compiles the voxel overworld out entirely.
VOXEL ?= 1
CPPFLAGS += -DCTR_VOXEL_ENABLED=$(VOXEL)
GAMEFLAGS += -DCTR_VOXEL_ENABLED=$(VOXEL)
# Baked fixed-sun lighting and contact shadows, targeting Old 3DS.
VOXEL_LIGHTING ?= 1
ifneq ($(VOXEL_LIGHTING),0)
ifneq ($(VOXEL_LIGHTING),1)
$(error VOXEL_LIGHTING must be 0 or 1)
endif
endif
ifeq ($(VOXEL),0)
override VOXEL_LIGHTING := 0
endif
CPPFLAGS += -DCTR_VOXEL_LIGHTING=$(VOXEL_LIGHTING)
GAMEFLAGS += -DCTR_VOXEL_LIGHTING=$(VOXEL_LIGHTING)
# A small FPS counter in the top-left corner of the top screen, shown when
# the bottom screen's options turn it on (off by default).
SHOW_FPS ?= 1
ifneq ($(SHOW_FPS),0)
ifneq ($(SHOW_FPS),1)
$(error SHOW_FPS must be 0 or 1)
endif
endif
CPPFLAGS += -DCTR_SHOW_FPS=$(SHOW_FPS)
# The options row that turns it on (3ds_bottom_ui.c).
GAMEFLAGS += -DCTR_SHOW_FPS=$(SHOW_FPS)
CPPFLAGS += -DCTR_DATA_BACKEND=$(DATA_BACKEND)
# Legacy code has existing warnings; platform code remains -Werror.
GAMECFLAGS := $(filter-out -Werror,$(CFLAGS)) -fsigned-char -fno-strict-aliasing
GAMECFLAGS += -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration
# Optional per-machine settings (e.g. SAMPLE_OVERLAY_DIR), never committed.
-include local.mk
include full.mk
OBJECTS += $(FULL_OBJECTS)

# Changing a stage/ABI flag cannot accidentally reuse objects from an older build.
.PHONY: FORCE
$(OBJECTS): build/config
build/config: FORCE
	@mkdir -p build
	@printf '%s\n' '$(CPPFLAGS) $(CFLAGS) $(GAMEFLAGS) $(GAMECFLAGS) $(LDFLAGS) $(LIBS)' > build/config.new
	@cmp -s build/config.new $@ || cp build/config.new $@
	@rm -f build/config.new

.PHONY: all clean verify release pak devdata
all: $(TARGET).3dsx

build/%.o: src/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# The linker script and specs are generated from emerald3ds.ld.in: the image
# link keeps .gamedata's contents, the shipped executable reserves it NOLOAD.
build/emerald3ds_image.ld: emerald3ds.ld.in
	@mkdir -p build
	sed 's/@GAMEDATA_TYPE@//' $< > $@
build/emerald3ds_release.ld: emerald3ds.ld.in
	@mkdir -p build
	sed 's/@GAMEDATA_TYPE@/(NOLOAD)/' $< > $@
build/emerald3ds_%.specs: build/emerald3ds_%.ld Makefile
	printf '%%include <sync-dmb.specs>\n\n*link:\n+ -T %s -d --emit-relocs --use-blx --gc-sections\n\n*startfile:\n3dsx_crt0%%O%%s crti%%O%%s crtbegin%%O%%s\n' '$<' > $@

# One object per song puts the link command well past what Windows will accept
# on a command line, so the object list goes through a response file.
$(TARGET).elf: $(OBJECTS) build/emerald3ds_release.specs
	@mkdir -p build
	$(file >build/link.rsp,$(OBJECTS))
	$(CC) $(LDFLAGS) -specs=build/emerald3ds_release.specs -Wl,-Map,build/$(TARGET).map @build/link.rsp $(LIBS) -o $@

build/gamedata_image.elf: $(OBJECTS) build/emerald3ds_image.specs
	@mkdir -p build
	$(file >build/link.rsp,$(OBJECTS))
	$(CC) $(LDFLAGS) -specs=build/emerald3ds_image.specs -Wl,-Map,build/gamedata_image.map @build/link.rsp $(LIBS) -o $@

# 3dsxtool builds its own relocation table from the ELF's; the NOLOAD region is
# relocated by the bundle loader instead, so its table stays out.
build/$(TARGET)_3dsx.elf: $(TARGET).elf
	$(DEVKITARM)/bin/arm-none-eabi-objcopy --remove-section=.rel.gamedata $< $@

$(TARGET).smdh: Makefile
	$(DEVKITPRO)/tools/bin/smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" assets/artwork/icon.png $@

ROMFS_DATA_OUTS := $(ROMFS_ASSET_OUTS) $(ROMFS_MAP_OUT) $(ROMFS_SCRIPT_OUTS) $(ROMFS_SOUND_OUTS) \
	$(ROMFS_SHADER_OUTS) $(ROMFS_GAMEDATA_OUTS)

# The ABI digests every game-data file, so a data pack is accepted only by the
# executable it was generated for.
romfs/engine/abi.bin: $(ROMFS_DATA_OUTS) $(SCRIPTS)/staging.py
	"$(PYTHON)" $(SCRIPTS)/staging.py abi --romfs romfs

ifeq ($(EMBED_DATA),1)
EMBED_MARKER := romfs/data.embedded
romfs/data.embedded:
	printf 'embedded\n' > $@
else
EMBED_MARKER :=
$(shell rm -f romfs/data.embedded)
endif

$(TARGET).3dsx: build/$(TARGET)_3dsx.elf $(TARGET).smdh romfs/boot.txt $(ROMFS_DATA_OUTS) romfs/engine/abi.bin $(EMBED_MARKER)
	$(DEVKITPRO)/tools/bin/3dsxtool $< $@ --smdh=$(TARGET).smdh --romfs=romfs

# Release: the executable with engine files only. Its game data is the data
# pack the player's builder generates.
release: build/$(TARGET)_3dsx.elf $(TARGET).smdh romfs/boot.txt $(ROMFS_DATA_OUTS) romfs/engine/abi.bin
	"$(PYTHON)" $(SCRIPTS)/staging.py engine --romfs romfs --out build/romfs_release
	@mkdir -p dist
	$(DEVKITPRO)/tools/bin/3dsxtool build/$(TARGET)_3dsx.elf dist/$(APP_FILE).3dsx \
		--smdh=$(TARGET).smdh --romfs=build/romfs_release
	cp $(TARGET).smdh dist/$(APP_FILE).smdh
	"$(PYTHON)" $(SCRIPTS)/staging.py list --romfs romfs > dist/staging-classification.txt

# The data pack, built from this tree's own staging: the release path, tested
# without the builder.
pak: romfs/engine/abi.bin
	"$(PYTHON)" $(SCRIPTS)/staging.py pak --romfs romfs --out build/emerald3ds.pak

# Loose game data for sdmc:/3ds/emerald3ds/devdata/ (see tools/dev_assets.py).
devdata: romfs/engine/abi.bin
	"$(PYTHON)" $(SCRIPTS)/staging.py devdata --romfs romfs --out build/devdata

verify: all verify-input
	"$(PYTHON)" scripts/verify_build.py --devkitarm "$(DEVKITARM)" --stage 9 --video 1 --full 1

.PHONY: verify-input
verify-input: build/input_test.exe
	./build/input_test.exe

build/input_test.exe: tests/input_test.c src/3ds_input_decode.c include/3ds_input.h
	@mkdir -p build
	$(HOSTCC) -std=c99 -O2 -Wall -Wextra -Werror -Iinclude tests/input_test.c src/3ds_input_decode.c -o $@

verify: verify-video
.PHONY: verify-video
verify-video: build/video_decode_test.exe
	./build/video_decode_test.exe

build/video_decode_test.exe: tests/video_decode_test.c src/3ds_video_decode.c include/3ds_video.h
	@mkdir -p build
	$(HOSTCC) -std=c99 -O2 -Wall -Wextra -Werror -Iinclude tests/video_decode_test.c src/3ds_video_decode.c -o $@

verify: verify-pak
.PHONY: verify-pak
verify-pak: build/pak_test.exe tests/make_test_pak.py
	./build/pak_test.exe build/test.pak $$("$(PYTHON)" tests/make_test_pak.py build/test.pak)

build/pak_test.exe: tests/pak_test.c src/3ds_pak.c include/3ds_pak.h
	@mkdir -p build
	$(HOSTCC) -std=c99 -O2 -Wall -Wextra -Werror -Iinclude tests/pak_test.c src/3ds_pak.c -o $@

clean:
	rm -rf build dist $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh

-include $(OBJECTS:.o=.d)
