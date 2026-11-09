# POSIX convenience wrappers. Python entry points also run without make.
.DEFAULT_GOAL := build
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SOURCE_ROOT ?= $(abspath $(ROOT)/../../../../..)
PYTHON ?= python3
OUTPUT ?= $(ROOT)/build/export

.PHONY: tools build patch-switcher inspect inspect-patch-switcher test boot-switch
tools:
	cd "$(SOURCE_ROOT)" && $(PYTHON) -m cntrl.maker.morningstar.mc.toolchain
build patch-switcher inspect inspect-patch-switcher boot-switch:
	cd "$(SOURCE_ROOT)" && $(PYTHON) -m cntrl.patch_switcher.cli build --target-dir "$(ROOT)/.." --source-root "$(SOURCE_ROOT)" --output "$(OUTPUT)"
test:
	cd "$(SOURCE_ROOT)" && $(PYTHON) -m cntrl.maker.morningstar.mc.native_tests --source-root "$(SOURCE_ROOT)"
