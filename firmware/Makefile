.DEFAULT_GOAL := test
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PYTHON ?= python3
.PHONY: test
test:
	cd "$(ROOT)/.." && $(PYTHON) -m pytest -q test
