COMPONENT := picoferm
OUTPUT_DIR ?= $(CURDIR)/build/ab
include ../../common/reference.mk

.PHONY: ab-test
ab-test: firmware-package
	PICOFERM_AB_BUILD="$(OUTPUT_DIR)" "$(PYTHON)" -m unittest discover -s tests -p 'test_ab_*.py' -v
