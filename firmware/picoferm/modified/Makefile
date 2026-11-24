COMPONENT := picoferm
include ../../common/reference.mk

AB_DIR ?= $(CURDIR)/build/ab
.PHONY: ab-test
ab-test:
	"$(PYTHON)" tools/build_esp8266.py --ab-layout --toolchain "$(TOOLCHAIN_DIR)" --arduino "$(ARDUINO_DIR)" --output "$(AB_DIR)"
	PICOFERM_AB_BUILD="$(AB_DIR)" "$(PYTHON)" -m unittest discover -s tests -p 'test_ab_*.py' -v
