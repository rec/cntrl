.DEFAULT_GOAL := test
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PROJECT_ROOT := $(abspath $(ROOT)/../../../../..)
TOOLS := $(ROOT)/../firmware/.tools

.PHONY: test check-board
test:
	mkdir -p "$(ROOT)/build"
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I"$(PROJECT_ROOT)/firmware/include" \
		"$(ROOT)/test/test_splash.cpp" -o "$(ROOT)/build/test_splash"
	"$(ROOT)/build/test_splash"

check-board:
	ARDUINO_DIRECTORIES_DATA="$(TOOLS)/data" \
	ARDUINO_DIRECTORIES_DOWNLOADS="$(TOOLS)/downloads" \
	ARDUINO_DIRECTORIES_USER="$(TOOLS)/user" \
	"$(TOOLS)/bin/arduino-cli" compile \
		--fqbn teensy:avr:teensy41:usb=serialmidi,speed=600,opt=osstd,keys=en-us \
		--build-path "$(ROOT)/build/check-board" \
		--build-property 'build.flags.cpp=-std=gnu++17 -fno-exceptions -fno-rtti -I$(ROOT) -I$(PROJECT_ROOT)/firmware/include -I$(ROOT)/../firmware/usb_probe' \
		--build-property 'recipe.hooks.postbuild.1.pattern=/usr/bin/true' \
		--build-property 'recipe.hooks.objcopy.postobjcopy.1.pattern=/usr/bin/true' \
		"$(ROOT)/compile_check"
