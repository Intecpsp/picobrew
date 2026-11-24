# Shared reproducible source/image/package targets. No target flashes hardware.
PYTHON ?= python3
COMMON_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DEPENDENCY_DIR ?= $(COMMON_DIR)/build/dependencies
OUTPUT_DIR ?= $(CURDIR)/build/reference
PACKAGE_DIR ?= $(CURDIR)/build/package
ESP8266_SDK_DIR ?= $(DEPENDENCY_DIR)/esp8266-2.4.1
ESP32_SDK_DIR ?= $(DEPENDENCY_DIR)/arduino-esp32-9a7946e6859130146e56231c27a4aa177d9175a1
ARDUINOJSON_DIR ?= $(DEPENDENCY_DIR)/ArduinoJson-0b03321ed33fa6965114424fbb023260120c856b
ETHERNET2_DIR ?= $(DEPENDENCY_DIR)/Ethernet2-0bab6349fb5b42fce2d5442bbed093279184b548

ifeq ($(COMPONENT),picoferm)
TOOLCHAIN_DIR ?= $(DEPENDENCY_DIR)/xtensa-lx106-elf
ARDUINO_DIR ?= $(ESP8266_SDK_DIR)
BUILD_SCRIPT := tools/build_esp8266.py
BUILD_EXTRA := --ab-layout
ELF_PATH := $(OUTPUT_DIR)/picoferm-standalone.elf
REPORT_PATH := $(OUTPUT_DIR)/build-report.json
PACKAGE_SDK_ARGS := --esp8266-sdk "$(ARDUINO_DIR)"
else
TOOLCHAIN_DIR ?= $(DEPENDENCY_DIR)/xtensa-esp32-elf
ARDUINO_DIR ?= $(ESP32_SDK_DIR)
PACKAGE_SDK_ARGS := --esp8266-sdk "$(ESP8266_SDK_DIR)" --esp32-sdk "$(ARDUINO_DIR)"
ifeq ($(COMPONENT),z-series)
BUILD_SCRIPT := tools/build_firmware.py
BUILD_EXTRA := --arduinojson "$(ARDUINOJSON_DIR)" --ethernet2 "$(ETHERNET2_DIR)"
ELF_PATH := $(OUTPUT_DIR)/link/application.elf
REPORT_PATH := $(OUTPUT_DIR)/standalone-build-report.json
else
BUILD_SCRIPT := tools/build_esp32.py
ELF_PATH := $(OUTPUT_DIR)/picostill-standalone.elf
REPORT_PATH := $(OUTPUT_DIR)/build-report.json
endif
endif

.PHONY: standalone target verify standalone-test bootstrap firmware-package web-test
standalone target:
	"$(PYTHON)" "$(BUILD_SCRIPT)" --toolchain "$(TOOLCHAIN_DIR)" --arduino "$(ARDUINO_DIR)" $(BUILD_EXTRA) --output "$(OUTPUT_DIR)"

verify standalone-test:
	ARDUINOJSON_DIR="$(ARDUINOJSON_DIR)" "$(PYTHON)" -m unittest discover -s tests -v

bootstrap:
	"$(PYTHON)" "$(COMMON_DIR)/tools/acquire_reference.py" --model "$(COMPONENT)" --destination "$(DEPENDENCY_DIR)"

firmware-package: standalone
ifeq ($(COMPONENT),picoferm)
	"$(PYTHON)" "$(COMMON_DIR)/tools/build_spiffs_image.py" --profile esp8266-2m --esp8266-sdk "$(ARDUINO_DIR)" --output "$(OUTPUT_DIR)/spiffs.bin"
	"$(PYTHON)" tools/package_ab.py "$(OUTPUT_DIR)" "$(OUTPUT_DIR)/spiffs.bin" "$(PACKAGE_DIR)/picoferm.zip"
else
	"$(PYTHON)" "$(COMMON_DIR)/tools/package_reference.py" --model "$(COMPONENT)" --elf "$(ELF_PATH)" --build-report "$(REPORT_PATH)" $(PACKAGE_SDK_ARGS) --output "$(PACKAGE_DIR)"
endif

web-test:
	"$(PYTHON)" "$(COMMON_DIR)/tests/web_browser.py"
