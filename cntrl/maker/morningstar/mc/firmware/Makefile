# Build-only tooling for macOS Apple Silicon. No upload target or device access.
.DEFAULT_GOAL := build

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
CLI := $(ROOT)/.tools/bin/arduino-cli
CORE := $(ROOT)/.tools/data/packages/teensy/hardware/avr/1.62.0
ARM := $(ROOT)/.tools/data/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi
INDEX := https://www.pjrc.com/teensy/package_teensy_index.json
FQBN := teensy:avr:teensy41:usb=serial,speed=600,opt=osstd,keys=en-us
IMAGE := $(ROOT)/build/usb-probe/usb_probe.ino
SCROLL_GAP ?= 72
PATCH_BUILD ?= $(ROOT)/build/patch-switcher
PATCH_OUTPUT ?= $(ROOT)/build/patch-switcher.hex
PATCH_LINKER ?=
SWITCH_PAIR ?= A+F
CABLE_TEST ?= 1
MIDI_INTERFACE ?= 1
ifneq ($(PATCH_LINKER),)
PATCH_LINK_OPTION = --build-property 'build.flags.ld=-Wl,--gc-sections,--relax,--no-warn-rwx-segments -T$(PATCH_LINKER)'
endif

export ARDUINO_DIRECTORIES_DATA := $(ROOT)/.tools/data
export ARDUINO_DIRECTORIES_DOWNLOADS := $(ROOT)/.tools/downloads
export ARDUINO_DIRECTORIES_USER := $(ROOT)/.tools/user
export ARDUINO_UPDATER_ENABLE_NOTIFICATION := false

.PHONY: tools build inspect test patch-switcher inspect-patch-switcher scroll-probe inspect-scroll-probe test-scroll-probe button-timing test-button-timing boot-switch test-switch-gesture test-boot-selection

boot-switch: tools test-switch-gesture test-boot-selection
	cd "$(ROOT)/../../../../.." && uv run --frozen -m cntrl.maker.morningstar.mc.boot_switch --pair '$(SWITCH_PAIR)'

test-switch-gesture:
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
		"$(ROOT)/test_switch_gesture.cpp" -o "$(ROOT)/build/test_switch_gesture"
	"$(ROOT)/build/test_switch_gesture"

test-boot-selection:
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
		"$(ROOT)/test_boot_selection.cpp" -o "$(ROOT)/build/test_boot_selection"
	"$(ROOT)/build/test_boot_selection"

button-timing: tools test-button-timing
	"$(CLI)" compile --fqbn "$(FQBN)" --warnings all \
		--build-path "$(ROOT)/build/button-timing" \
		--build-property 'build.flags.c=-DCNTRL_EXTERNAL_FLASH_CONFIG -I$(ROOT)' \
		--build-property 'build.flags.cpp=-std=gnu++17 -fno-exceptions -fpermissive -fno-rtti -fno-threadsafe-statics -felide-constructors -Wno-error=narrowing -Wno-psabi -Wno-maybe-uninitialized -I$(ROOT) -I$(ROOT)/usb_probe' \
		--build-property 'recipe.hooks.postbuild.1.pattern=/usr/bin/true' \
		--build-property 'recipe.hooks.objcopy.postobjcopy.1.pattern=/usr/bin/true' \
		"$(ROOT)/button_timing"
	"$(ARM)-objcopy" -O binary "$(ROOT)/build/button-timing/button_timing.ino.elf" "$(ROOT)/build/button-timing/image.bin"
	"$(ARM)-objcopy" -I ihex -O binary "$(ROOT)/build/button-timing/button_timing.ino.hex" "$(ROOT)/build/button-timing/from-hex.bin"
	cmp "$(ROOT)/build/button-timing/image.bin" "$(ROOT)/build/button-timing/from-hex.bin"
	dd if="$(ROOT)/build/button-timing/image.bin" bs=512 count=1 2>/dev/null | shasum -a 256 -c "$(ROOT)/stock-flash-config.sha256"

test-button-timing:
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
		"$(ROOT)/test_pair_timing.cpp" -o "$(ROOT)/build/test_pair_timing"
	"$(ROOT)/build/test_pair_timing"

scroll-probe: tools
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/no-external-ram.patch"
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/external-flash-config.patch"
	"$(CLI)" compile --fqbn teensy:avr:teensy41:usb=serialmidi,speed=600,opt=osstd,keys=en-us --warnings all \
		--build-path "$(ROOT)/build/scroll-probe" \
		--build-property 'build.flags.c=-DCNTRL_EXTERNAL_FLASH_CONFIG -I$(ROOT)/usb_probe' \
		--build-property 'build.flags.cpp=-std=gnu++17 -fno-exceptions -fpermissive -fno-rtti -fno-threadsafe-statics -felide-constructors -Wno-error=narrowing -Wno-psabi -Wno-maybe-uninitialized -DCNTRL_SCROLL_GAP=$(SCROLL_GAP) -I$(ROOT)/usb_probe' \
		--build-property 'recipe.hooks.postbuild.1.pattern=/usr/bin/true' \
		--build-property 'recipe.hooks.objcopy.postobjcopy.1.pattern=/usr/bin/true' \
		"$(ROOT)/scroll_probe"
	cp "$(ROOT)/build/scroll-probe/scroll_probe.ino.hex" "$(ROOT)/build/scroll-probe.hex"

inspect-scroll-probe: scroll-probe
	"$(ARM)-objcopy" -O binary "$(ROOT)/build/scroll-probe/scroll_probe.ino.elf" "$(ROOT)/build/scroll-probe/image.bin"
	"$(ARM)-objcopy" -I ihex -O binary "$(ROOT)/build/scroll-probe.hex" "$(ROOT)/build/scroll-probe/from-hex.bin"
	cmp "$(ROOT)/build/scroll-probe/image.bin" "$(ROOT)/build/scroll-probe/from-hex.bin"
	dd if="$(ROOT)/build/scroll-probe/image.bin" bs=512 count=1 2>/dev/null | shasum -a 256 -c "$(ROOT)/stock-flash-config.sha256"
	shasum -a 256 "$(ROOT)/build/scroll-probe.hex"
	"$(ARM)-size" -A "$(ROOT)/build/scroll-probe/scroll_probe.ino.elf"

test-scroll-probe: tools
	mkdir -p "$(ROOT)/build"
	$(CC) -c "$(CORE)/libraries/ST7735_t3/src/glcdfont.c" -o "$(ROOT)/build/scroll-font.o"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -DCNTRL_SCROLL_GAP=$(SCROLL_GAP) \
		"$(ROOT)/test_scroll_probe.cpp" "$(ROOT)/build/scroll-font.o" -o "$(ROOT)/build/test_scroll_probe"
	"$(ROOT)/build/test_scroll_probe"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -DCNTRL_SCROLL_PROBE -I"$(ROOT)/test_support" -I"$(ROOT)/usb_probe" \
		"$(ROOT)/test_displays.cpp" "$(ROOT)/scroll_probe/displays.cpp" -o "$(ROOT)/build/test_scroll_displays"
	"$(ROOT)/build/test_scroll_displays"

patch-switcher: tools
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/usb-midi-interface.patch" > /dev/null 2>&1 || \
		patch --force --fuzz=0 -d "$(CORE)" -p1 < "$(ROOT)/usb-midi-interface.patch"
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/fonts-in-flash.patch" > /dev/null 2>&1 || \
		patch --force --fuzz=0 -d "$(CORE)" -p1 < "$(ROOT)/fonts-in-flash.patch"
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/no-external-ram.patch"
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/external-flash-config.patch"
	"$(CLI)" compile --fqbn teensy:avr:teensy41:usb=serialmidi,speed=600,opt=osstd,keys=en-us --warnings all \
		--build-path "$(PATCH_BUILD)" \
		$(PATCH_LINK_OPTION) \
		--build-property 'build.flags.c=-DCNTRL_EXTERNAL_FLASH_CONFIG -DCNTRL_USB_INTERFACE -I$(ROOT)/usb_probe' \
		--build-property 'build.flags.cpp=-std=gnu++17 -fno-exceptions -fpermissive -fno-rtti -fno-threadsafe-statics -felide-constructors -Wno-error=narrowing -Wno-psabi -Wno-maybe-uninitialized -DCNTRL_USB_INTERFACE -DCNTRL_CABLE_TEST=$(CABLE_TEST) -DCNTRL_MIDI_INTERFACE=$(MIDI_INTERFACE) -I$(abspath $(ROOT)/../../../../..)/firmware/include -I$(ROOT)/usb_probe' \
		--build-property 'recipe.hooks.postbuild.1.pattern=/usr/bin/true' \
		--build-property 'recipe.hooks.objcopy.postobjcopy.1.pattern=/usr/bin/true' \
		"$(ROOT)/patch_switcher"
	cp "$(PATCH_BUILD)/patch_switcher.ino.hex" "$(PATCH_OUTPUT)"

inspect-patch-switcher: patch-switcher
	"$(ARM)-objcopy" -O binary "$(ROOT)/build/patch-switcher/patch_switcher.ino.elf" "$(ROOT)/build/patch-switcher/image.bin"
	"$(ARM)-objcopy" -I ihex -O binary "$(ROOT)/build/patch-switcher.hex" "$(ROOT)/build/patch-switcher/from-hex.bin"
	cmp "$(ROOT)/build/patch-switcher/image.bin" "$(ROOT)/build/patch-switcher/from-hex.bin"
	dd if="$(ROOT)/build/patch-switcher/image.bin" bs=512 count=1 2>/dev/null | shasum -a 256 -c "$(ROOT)/stock-flash-config.sha256"
	shasum -a 256 "$(ROOT)/build/patch-switcher.hex"
	"$(ARM)-size" -A "$(ROOT)/build/patch-switcher/patch_switcher.ino.elf"
	"$(ARM)-nm" -n -C "$(ROOT)/build/patch-switcher/patch_switcher.ino.elf" > "$(ROOT)/build/patch-switcher/symbols.txt"

test: tools test-button-timing test-switch-gesture test-boot-selection
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror "$(ROOT)/test_buttons.cpp" -o "$(ROOT)/build/test_buttons"
	"$(ROOT)/build/test_buttons"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(ROOT)/test_support" \
		"$(ROOT)/test_displays.cpp" "$(ROOT)/usb_probe/displays.cpp" -o "$(ROOT)/build/test_displays"
	"$(ROOT)/build/test_displays"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -DCNTRL_PANORAMA -I"$(ROOT)/test_support" -I"$(ROOT)/usb_probe" \
		"$(ROOT)/test_displays.cpp" "$(ROOT)/patch_switcher/displays.cpp" -o "$(ROOT)/build/test_panorama_displays"
	"$(ROOT)/build/test_panorama_displays"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-I"$(abspath $(ROOT)/../../../../..)/firmware/include" -I"$(ROOT)/usb_probe" -I"$(ROOT)/test_support" \
		-I"$(CORE)/libraries/ST7735_t3/src" -x c++ \
		"$(ROOT)/test_renderer.cpp" $(wildcard $(ROOT)/patch_switcher/font_*.cpp) "$(CORE)/libraries/ST7735_t3/src/st7735_t3_font_Arial.c" \
		"$(CORE)/libraries/ST7735_t3/src/st7735_t3_font_ComicSansMS.c" -o "$(ROOT)/build/test_renderer"
	"$(ROOT)/build/test_renderer"

tools: $(ROOT)/.tools/flash-config-ready

$(CLI):
	test "$$(uname -sm)" = 'Darwin arm64'
	mkdir -p "$(ROOT)/.tools/downloads" "$(ROOT)/.tools/bin"
	curl -fL https://github.com/arduino/arduino-cli/releases/download/v1.5.1/arduino-cli_1.5.1_macOS_ARM64.tar.gz -o "$(ROOT)/.tools/downloads/arduino-cli.tar.gz"
	printf '%s  %s\n' cb952e8c1621c95ef5f1d17831c945e3d0ec5973f89c557a7ec8feb9c4f7d4c9 "$(ROOT)/.tools/downloads/arduino-cli.tar.gz" | shasum -a 256 -c -
	tar -xzf "$(ROOT)/.tools/downloads/arduino-cli.tar.gz" -C "$(ROOT)/.tools/bin" arduino-cli

$(ROOT)/.tools/core-ready: $(CLI) $(ROOT)/no-external-ram.patch
	"$(CLI)" core update-index --additional-urls "$(INDEX)"
	"$(CLI)" core install teensy:avr@1.62.0 --additional-urls "$(INDEX)"
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/no-external-ram.patch" > /dev/null 2>&1 || \
		patch --force --fuzz=0 -d "$(CORE)" -p1 < "$(ROOT)/no-external-ram.patch"
	touch "$@"

$(ROOT)/.tools/flash-config-ready: $(ROOT)/.tools/core-ready $(ROOT)/external-flash-config.patch $(ROOT)/Makefile
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/external-flash-config.patch" > /dev/null 2>&1 || \
		patch --force --fuzz=0 -d "$(CORE)" -p1 < "$(ROOT)/external-flash-config.patch"
	touch "$@"

build: tools
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/no-external-ram.patch"
	patch --force --fuzz=0 --dry-run --reverse -d "$(CORE)" -p1 < "$(ROOT)/external-flash-config.patch"
	"$(CLI)" compile --fqbn "$(FQBN)" --warnings all \
		--build-path "$(ROOT)/build/usb-probe" \
		--build-property 'build.flags.c=-DCNTRL_EXTERNAL_FLASH_CONFIG' \
		--build-property 'recipe.hooks.postbuild.1.pattern=/usr/bin/true' \
		--build-property 'recipe.hooks.objcopy.postobjcopy.1.pattern=/usr/bin/true' \
		"$(ROOT)/usb_probe"
	cp "$(IMAGE).hex" "$(ROOT)/build/usb_probe.ino.hex"

inspect: build
	"$(ARM)-objcopy" -O binary "$(IMAGE).elf" "$(IMAGE).bin"
	"$(ARM)-objcopy" -I ihex -O binary "$(IMAGE).hex" "$(ROOT)/build/from-hex.bin"
	cmp "$(IMAGE).bin" "$(ROOT)/build/from-hex.bin"
	dd if="$(IMAGE).bin" bs=512 count=1 2>/dev/null | shasum -a 256 -c "$(ROOT)/stock-flash-config.sha256"
	shasum -a 256 "$(IMAGE).elf" "$(IMAGE).hex" "$(IMAGE).bin"
	"$(ARM)-readelf" -h -l "$(IMAGE).elf"
	"$(ARM)-size" -A "$(IMAGE).elf"
	"$(ARM)-nm" -n -C "$(IMAGE).elf" > "$(ROOT)/build/symbols.txt"
	"$(ARM)-objdump" -d -C "$(IMAGE).elf" > "$(ROOT)/build/disassembly.txt"
