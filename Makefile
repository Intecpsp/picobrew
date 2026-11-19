PYTHON ?= python3
GO ?= go
NPM ?= npm
MACHINE ?= pico-s-pro
MODIFIED := firmware/$(MACHINE)/modified
OEM_SOURCE := firmware/$(MACHINE)/reverse-engineered

.DEFAULT_GOAL := help
.PHONY: help firmware firmware-test firmware-usb-test firmware-browser-test firmware-bootstrap oem server server-test server-run server-demo verify-imports

help:
	@printf '%s\n' 'make firmware / firmware-test / oem MACHINE=pico-s-pro' 'make server / server-test / server-run / server-demo' 'make verify-imports'

firmware:
	$(MAKE) -C $(MODIFIED) standalone PYTHON="$(PYTHON)"

firmware-test:
	$(MAKE) -C $(MODIFIED) standalone-test PYTHON="$(PYTHON)"

firmware-usb-test:
	$(MAKE) -C $(MODIFIED) usb-test PYTHON="$(PYTHON)"

firmware-browser-test:
	$(MAKE) -C $(MODIFIED) web-test PYTHON="$(PYTHON)"

firmware-bootstrap:
	$(MAKE) -C $(MODIFIED) bootstrap PYTHON="$(PYTHON)"

oem:
	$(PYTHON) $(OEM_SOURCE)/tools/build_firmware.py

server:
	$(MAKE) -C server build GO="$(GO)" NPM="$(NPM)"

server-test:
	$(MAKE) -C server test GO="$(GO)" NPM="$(NPM)" PYTHON="$(PYTHON)"

server-run:
	$(MAKE) -C server run GO="$(GO)"

server-demo:
	$(MAKE) -C server demo GO="$(GO)"

verify-imports:
	$(PYTHON) tools/verify_firmware_import.py
