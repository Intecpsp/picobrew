PYTHON ?= python3
CXX ?= c++
AR ?= ar
CPPFLAGS += -Iinclude
CXXFLAGS += -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fno-fast-math -ffp-contract=off
SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(patsubst src/%.cpp,build/%.o,$(SOURCES))

.PHONY: all analyze test verify clean
# Owned application modules, not a complete ESP8266 firmware image.
all: build/libpicoferm_oem.a

build/libpicoferm_oem.a: $(OBJECTS)
	rm -f $@
	"$(AR)" rcs $@ $^

build/%.o: src/%.cpp $(wildcard include/picoferm/*.hpp)
	@mkdir -p build
	"$(CXX)" $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

analyze:
	"$(PYTHON)" tools/analyze_images.py

test:
	"$(PYTHON)" -m unittest discover -s tests -p 'test_*.py'

verify: all test
	"$(PYTHON)" tools/analyze_images.py --check

clean:
	rm -f build/*.o build/libpicoferm_oem.a
