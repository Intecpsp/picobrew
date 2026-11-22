PYTHON ?= python3
HUB_HOST ?= picobrew.com
.PHONY: standalone firmware-package standalone-test usb-test web-test bootstrap

standalone firmware-package:
	"$(PYTHON)" tools/package.py --hub-host "$(HUB_HOST)" $(if $(OEM_FILE),--oem-file "$(OEM_FILE)") $(if $(KEY_FILE),--key-file "$(KEY_FILE)") $(if $(DEVICE_UID),--uid "$(DEVICE_UID)") $(if $(OFFLINE_PROGRAM),--offline-program "$(OFFLINE_PROGRAM)")

standalone-test usb-test web-test:
	"$(PYTHON)" -m unittest discover -s tests

bootstrap:
	"$(PYTHON)" -m pip install pyserial==3.5
