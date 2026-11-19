ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif
GO ?= go
GO_TEST_FLAGS ?=
NPM ?= npm
MACHINE ?= pico-s-pro
MODIFIED := firmware/$(MACHINE)/modified
OEM_SOURCE := firmware/$(MACHINE)/reverse-engineered

.DEFAULT_GOAL := help
.PHONY: help firmware firmware-test firmware-usb-test firmware-browser-test firmware-bootstrap oem recovery server server-start server-test server-run server-demo verify-imports

help:
	@echo make server-start - build and run the Hub
	@echo make firmware / firmware-test / oem / recovery MACHINE=pico-s-pro
	@echo make server / server-test / server-run / server-demo
	@echo make verify-imports

firmware:
	"$(MAKE)" -C $(MODIFIED) standalone PYTHON="$(PYTHON)"

firmware-test:
	"$(MAKE)" -C $(MODIFIED) standalone-test PYTHON="$(PYTHON)"

firmware-usb-test:
	"$(MAKE)" -C $(MODIFIED) usb-test PYTHON="$(PYTHON)"

firmware-browser-test:
	"$(MAKE)" -C $(MODIFIED) web-test PYTHON="$(PYTHON)"

firmware-bootstrap:
	"$(MAKE)" -C $(MODIFIED) bootstrap PYTHON="$(PYTHON)"

oem:
	"$(PYTHON)" tools/build_archive.py oem --machine "$(MACHINE)"

recovery:
	"$(PYTHON)" tools/build_archive.py recovery --machine "$(MACHINE)"

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
