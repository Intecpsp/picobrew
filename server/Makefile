GO ?= go
GO_TEST_FLAGS ?=
NPM ?= npm
ifeq ($(OS),Windows_NT)
PYTHON ?= python
HUB_BINARY := picobrew-hub.exe
HUB_COMMAND := .\picobrew-hub.exe
else
PYTHON ?= python3
HUB_BINARY := picobrew-hub
HUB_COMMAND := ./picobrew-hub
endif
.PHONY: build server-start test run demo web
build:
	"$(GO)" run ./tools/usb-prepare -local "$(USB_WORKER_LOCAL)"
	"$(GO)" build -trimpath -o "$(HUB_BINARY)" ./cmd/picobrew-hub
server-start: build
	"$(HUB_COMMAND)" --data ./data
web:
	"$(NPM)" --prefix web ci
	"$(NPM)" --prefix web run build:bundle
test:
	"$(GO)" test $(GO_TEST_FLAGS) ./...
	"$(PYTHON)" -m unittest discover -s tools -p 'test_*.py'
	"$(NPM)" --prefix web test
run: server-start
demo: build
	"$(HUB_COMMAND)" --demo --data demo-data
