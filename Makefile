ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif
GO ?= go
GO_TEST_FLAGS ?=
NPM ?= npm
MACHINE ?=
MODEL ?=
ARCHIVE_MACHINE := $(if $(strip $(MACHINE)),$(MACHINE),pico-s-pro)
MODEL_ARGS = --machine "$(MACHINE)" --model "$(MODEL)" --make "$(MAKE)" --python "$(PYTHON)"

.DEFAULT_GOAL := help
.PHONY: help firmware firmware-package firmware-models firmware-test firmware-usb-test firmware-browser-test firmware-bootstrap oem recovery server server-start server-test server-run server-demo verify-imports

help:
	@echo make server-start - build and run the Hub
	@echo make firmware / firmware-test MODEL=pico-pro or MODEL=pico-s
	@echo make firmware-package MODEL=pico-s - build and package a Hub upload ZIP
	@echo make firmware-models / oem / recovery MACHINE=pico-s-pro
	@echo make server / server-test / server-run / server-demo
	@echo make verify-imports

firmware:
	"$(PYTHON)" tools/firmware_models.py build $(MODEL_ARGS)

firmware-package:
	"$(PYTHON)" tools/firmware_models.py package $(MODEL_ARGS)

firmware-models:
	"$(PYTHON)" tools/firmware_models.py list

firmware-test:
	"$(PYTHON)" tools/firmware_models.py test $(MODEL_ARGS)

firmware-usb-test:
	"$(PYTHON)" tools/firmware_models.py usb-test $(MODEL_ARGS)

firmware-browser-test:
	"$(PYTHON)" tools/firmware_models.py browser-test $(MODEL_ARGS)

firmware-bootstrap:
	"$(PYTHON)" tools/firmware_models.py bootstrap $(MODEL_ARGS)

oem:
	"$(PYTHON)" tools/build_archive.py oem --machine "$(ARCHIVE_MACHINE)"

recovery:
	"$(PYTHON)" tools/build_archive.py recovery --machine "$(ARCHIVE_MACHINE)"

server:
	"$(MAKE)" -C server build GO="$(GO)"

server-start:
	"$(MAKE)" -C server server-start GO="$(GO)"

server-test:
	"$(MAKE)" -C server test GO="$(GO)" GO_TEST_FLAGS="$(GO_TEST_FLAGS)" NPM="$(NPM)" PYTHON="$(PYTHON)"

server-run:
	"$(MAKE)" -C server run GO="$(GO)"

server-demo:
	"$(MAKE)" -C server demo GO="$(GO)"

verify-imports:
	"$(PYTHON)" tools/verify_firmware_import.py
