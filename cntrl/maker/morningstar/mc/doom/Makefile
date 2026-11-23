# SPDX-License-Identifier: GPL-2.0-or-later
.DEFAULT_GOAL := firmware
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DMC ?= $(ROOT)/upstream
OUT ?= $(ROOT)/build
TOOLS ?= $(ROOT)/../firmware/.tools
PYTHON ?= python3
PACK := $(DMC)/build/packs/E1M1
NOR := $(OUT)/e1m1.img
SKETCH := $(OUT)/doom_demo
SDK := $(TOOLS)/data/packages/teensy/tools/teensy-compile/15.2.1/arm/bin
FLASH_CONFIG := $(ROOT)/../firmware/usb_probe/flash_config.c

ENGINE := $(filter-out %/w_file.c %/w_wad.c,$(wildcard $(DMC)/engine/*.c))
PORT := $(wildcard $(DMC)/port/data/*.c $(DMC)/port/obj/*.c $(DMC)/port/render/*.c)
PORT += $(DMC)/port/math/trig.c $(DMC)/port/ovl/ovl.c $(DMC)/port/ovl/ovl_file.c
PORT += $(addprefix $(DMC)/port/game/,fe_glue.c l_menu.c l_wi.c l_finale.c l_save.c g_save.c l_level.c)
GEN := $(addprefix $(DMC)/gen/,info.c colormap.c trig_tables.c view.c)
SOURCES := $(ENGINE) $(PORT) $(GEN)
HEADERS := $(wildcard $(DMC)/engine/*.h $(DMC)/port/*/*.h $(DMC)/platform/*.h $(DMC)/gen/*.h)
INC := $(foreach dir,engine port/data port/obj port/render port/math port/ovl port/game platform gen,"-I$(DMC)/$(dir)")
FLAGS := -std=gnu11 -fwrapv -fsigned-char -DDMC_STATS -DPLAT_HOST -DR_FRAME_HOST=65536 -DO_ARENA_MAX=65536
SHARED := game.c video.c storage.c

export ARDUINO_DIRECTORIES_DATA := $(TOOLS)/data
export ARDUINO_DIRECTORIES_DOWNLOADS := $(TOOLS)/downloads
export ARDUINO_DIRECTORIES_USER := $(TOOLS)/user
export ARDUINO_UPDATER_ENABLE_NOTIFICATION := false

.PHONY: firmware check relocated host
relocated:
	$(MAKE) firmware OUT="$(OUT)/relocated" FLASH_ORIGIN=0x60390000 FLASH_LENGTH=4288K

ifneq ($(FLASH_ORIGIN),)
CORE_LINKER := $(TOOLS)/data/packages/teensy/hardware/avr/1.62.0/cores/teensy4/imxrt1062_t41.ld
LINKER := $(OUT)/doom.ld
LINK_PROPERTY := --build-property 'build.flags.ld=-Wl,--gc-sections,--relax,--no-warn-rwx-segments "-T$(LINKER)"'
RETURN_FLAGS := -DCNTRL_DOOM_RETURN "-I$(ROOT)/../firmware"
RETURN_HEADERS := return_button.h $(ROOT)/../firmware/boot_switch/selection.h

$(LINKER): $(CORE_LINKER) Makefile
	mkdir -p "$(OUT)"
	sed 's/ORIGIN = 0x60000000, LENGTH = 7936K/ORIGIN = $(FLASH_ORIGIN), LENGTH = $(FLASH_LENGTH)/' "$<" > "$@"
endif

firmware: $(OUT)/elf/doom_demo.ino.elf
	"$(SDK)/arm-none-eabi-size" "$<"
	"$(SDK)/arm-none-eabi-objcopy" -O binary "$<" "$(OUT)/elf.bin"
	"$(SDK)/arm-none-eabi-objcopy" -I ihex -O binary "$(OUT)/elf/doom_demo.ino.hex" "$(OUT)/hex.bin"
	cmp "$(OUT)/elf.bin" "$(OUT)/hex.bin"

$(OUT)/elf/doom_demo.ino.elf: $(SOURCES) $(HEADERS) $(SHARED) board.cpp demo.h Makefile $(NOR) $(FLASH_CONFIG) $(LINKER) $(RETURN_HEADERS)
	mkdir -p "$(SKETCH)/src"
	touch "$(SKETCH)/doom_demo.ino"
	cp $(SHARED) board.cpp demo.h return_button.h "$(FLASH_CONFIG)" "$(SKETCH)/src/"
	cd "$(DMC)" && for source in $(patsubst $(DMC)/%,%,$(SOURCES)); do \
		mkdir -p "$(SKETCH)/src/$$(dirname "$$source")" && \
		cp "$$source" "$(SKETCH)/src/$$source" || exit; done
	printf '.section .progmem.doom,"a",%%progbits\n.balign 4\n.global demo_rom\ndemo_rom:\n.incbin "%s"\n' "$(NOR)" > "$(SKETCH)/src/rom.S"
	"$(TOOLS)/bin/arduino-cli" compile --fqbn teensy:avr:teensy41:usb=serial,speed=600,opt=osstd,keys=en-us \
		--build-path "$(OUT)/elf" \
		$(LINK_PROPERTY) \
		--build-property 'build.flags.c=$(FLAGS) -DCNTRL_EXTERNAL_FLASH_CONFIG $(INC)' \
		--build-property 'build.flags.cpp=-std=gnu++17 -fno-exceptions -fno-rtti $(INC) $(RETURN_FLAGS)' \
		--build-property 'recipe.hooks.objcopy.postobjcopy.1.pattern=' \
		--build-property 'recipe.hooks.postbuild.1.pattern=' \
		--build-property 'recipe.hooks.postbuild.4.pattern=' "$(SKETCH)"

$(NOR): $(PACK)/DOOM.DAT $(DMC)/tests/host/mknor.py
	mkdir -p "$(OUT)"
	$(PYTHON) -I "$(DMC)/tests/host/mknor.py" "$(PACK)" "$@"

$(OUT)/check: $(SOURCES) $(HEADERS) $(SHARED) check.c demo.h Makefile
	mkdir -p "$(OUT)"
	$(CC) -O2 -g $(CFLAGS) $(FLAGS) $(INC) -Wno-unused-value -Wno-pointer-sign \
		$(SOURCES) $(SHARED) check.c $(LDFLAGS) -lm -o "$@"

$(OUT)/test-return: test_return.cpp return_button.h Makefile
	mkdir -p "$(OUT)"
	$(CXX) $(CXXFLAGS) -std=c++17 test_return.cpp -o "$@"

check: $(OUT)/check $(NOR) $(OUT)/test-return
	"$<" "$(NOR)"
	"$(OUT)/test-return"

$(OUT)/doom-host: $(SOURCES) $(HEADERS) $(SHARED) host.c demo.h Makefile
	mkdir -p "$(OUT)"
	$(CC) -O2 -g $(CFLAGS) $(FLAGS) $(INC) -Wno-unused-value -Wno-pointer-sign \
		$(SOURCES) $(SHARED) host.c $(LDFLAGS) -lm -o "$@"

host: $(OUT)/doom-host $(NOR)
