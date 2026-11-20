PYTHON ?= python3
PICOBREW_SDK ?= .deps/energia
PICOBREW_ARM_PREFIX ?= .deps/toolchain/bin/arm-none-eabi-
DISPLAY ?= ssd1325
export PICOBREW_SDK PICOBREW_ARM_PREFIX

.PHONY: all bootstrap firmware verify host-test target-test clean
all: firmware
bootstrap:
	"$(PYTHON)" tools/bootstrap_dependencies.py
firmware:
	"$(PYTHON)" tools/build_firmware.py --display "$(DISPLAY)"
host-test:
	"$(PYTHON)" tools/verify.py --host-only
target-test:
	"$(PYTHON)" tools/verify.py --display "$(DISPLAY)" --target-only
verify:
	"$(PYTHON)" tools/verify.py --display both
clean:
	"$(PYTHON)" -c "from pathlib import Path; import shutil; shutil.rmtree('build', ignore_errors=True)"
