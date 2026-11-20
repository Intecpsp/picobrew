ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif
MODEL ?= pico-c
DISPLAY_VARIANT ?=
TEST_DISPLAY = $(if $(strip $(DISPLAY_VARIANT)),$(DISPLAY_VARIANT),both)
STANDALONE_DIR = build/standalone/$(MODEL)/$(DISPLAY_VARIANT)/firmware

.PHONY: all bootstrap standalone firmware-package standalone-test standalone-arm-test standalone-storage-test standalone-verify web-test tooling-test
.NOTPARALLEL: standalone-verify
all: standalone

standalone:
	"$(PYTHON)" tools/build_firmware.py --standalone --model "$(MODEL)" --display "$(DISPLAY_VARIANT)"

firmware-package: standalone
	"$(PYTHON)" tools/package_hub_firmware.py --model "$(MODEL)" --display "$(DISPLAY_VARIANT)" --firmware-directory "$(STANDALONE_DIR)"

standalone-test:
	"$(PYTHON)" tools/generate_starter_recipes.py --check
	"$(PYTHON)" tools/generate_api_explorer.py --check
	"$(PYTHON)" tools/embed_web.py --check
	"$(PYTHON)" tests/test_http_server.py
	"$(PYTHON)" tests/test_energia_http_server.py
	"$(PYTHON)" tests/test_energia_wifi.py
	"$(PYTHON)" tests/test_local_api.py
	"$(PYTHON)" tests/test_machine_identity.py
	"$(PYTHON)" tests/test_local_history.py
	"$(PYTHON)" tests/test_local_telemetry.py
	"$(PYTHON)" tests/test_telemetry_receiver.py
	"$(PYTHON)" tests/test_standalone_control.py --display "$(TEST_DISPLAY)"

standalone-arm-test:
	"$(PYTHON)" tests/test_standalone_control_arm.py

standalone-storage-test: standalone
	"$(PYTHON)" tests/test_native_storage.py --elf "$(STANDALONE_DIR)/pico-c-standalone.elf"

standalone-verify: standalone-test standalone-arm-test standalone-storage-test
	"$(PYTHON)" tools/verify_firmware_package.py --model "$(MODEL)" --display "$(DISPLAY_VARIANT)"

web-test:
	"$(PYTHON)" tests/test_web.py
	"$(PYTHON)" tests/test_web_features.py

tooling-test:
	"$(PYTHON)" tests/test_machine_identity.py
	"$(PYTHON)" tests/test_hub_firmware_package.py

bootstrap:
	"$(PYTHON)" tools/bootstrap_dependencies.py
