# Build-only tooling for macOS Apple Silicon. No upload target or device access.
.DEFAULT_GOAL := build

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
CLI := $(ROOT)/.tools/bin/arduino-cli
CORE := $(ROOT)/.tools/data/packages/teensy/hardware/avr/1.62.0
ARM := $(ROOT)/.tools/data/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi
INDEX := https://www.pjrc.com/teensy/package_teensy_index.json
FQBN := teensy:avr:teensy41:usb=serial,speed=600,opt=osstd,keys=en-us
IMAGE := $(ROOT)/build/usb_probe.ino

export ARDUINO_DIRECTORIES_DATA := $(ROOT)/.tools/data
export ARDUINO_DIRECTORIES_DOWNLOADS := $(ROOT)/.tools/downloads
export ARDUINO_DIRECTORIES_USER := $(ROOT)/.tools/user
export ARDUINO_UPDATER_ENABLE_NOTIFICATION := false

.PHONY: tools build inspect

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
		--build-path "$(ROOT)/build" \
		--build-property 'build.flags.c=-DCNTRL_EXTERNAL_FLASH_CONFIG' \
		--build-property 'recipe.hooks.postbuild.1.pattern=/usr/bin/true' \
		--build-property 'recipe.hooks.objcopy.postobjcopy.1.pattern=/usr/bin/true' \
		"$(ROOT)/usb_probe"

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
