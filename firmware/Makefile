.DEFAULT_GOAL := test
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PYTHON ?= python3
.PHONY: test
test:
	$(PYTHON) "$(ROOT)/../scripts/native_tests.py" --source-root "$(ROOT)/.."
