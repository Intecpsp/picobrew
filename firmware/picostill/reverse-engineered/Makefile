PYTHON ?= python3
CXX ?= c++
AR ?= ar
CPPFLAGS += -Iinclude
CXXFLAGS += -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fno-fast-math -ffp-contract=off
SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(patsubst src/%.cpp,build/%.o,$(SOURCES))

.PHONY: all analyze test verify clean
# Portable native archive. The pinned ESP32 object/link tools build the full
# target application separately; neither path produces an installation package.
all: build/libpicostill_oem.a

build/libpicostill_oem.a: $(OBJECTS)
	rm -f $@
	"$(AR)" rcs $@ $^

build/%.o: src/%.cpp $(wildcard include/picostill/*.hpp)
	@mkdir -p build
	"$(CXX)" $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

analyze:
	"$(PYTHON)" tools/analyze_images.py
	"$(PYTHON)" tools/recover_enums.py
	"$(PYTHON)" tools/recover_web.py
	"$(PYTHON)" tools/recover_animations.py

test:
	"$(PYTHON)" -m unittest discover -s tests -p 'test_*.py'

verify: all test
	"$(PYTHON)" tools/analyze_images.py --check
	"$(PYTHON)" tools/recover_enums.py --check
	"$(PYTHON)" tools/recover_web.py --check
	"$(PYTHON)" tools/recover_animations.py --check

clean:
	rm -f build/*.o build/libpicostill_oem.a
