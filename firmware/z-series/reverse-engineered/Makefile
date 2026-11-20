PYTHON ?= python3
CXX ?= c++
CC ?= cc
AR ?= ar
ARDUINOJSON_DIR ?= build/dependencies/ArduinoJson-0b03321ed33fa6965114424fbb023260120c856b
CPPFLAGS += -Iinclude
CXXFLAGS += -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fno-fast-math -ffp-contract=off
CFLAGS += -std=c11 -O2 -Wall -Wextra -Werror -pedantic
CPP_SOURCES := $(wildcard src/*.cpp)
C_SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst src/%.cpp,build/%.o,$(CPP_SOURCES)) $(patsubst src/%.c,build/%.o,$(C_SOURCES))
OWNED_INCLUDES := $(wildcard include/zseries/*.hpp include/zseries/*.h src/*.inc)
JSON_SOURCES := $(wildcard adapters/arduinojson/*.cpp)
JSON_OBJECTS := $(patsubst adapters/arduinojson/%.cpp,build/json_%.o,$(JSON_SOURCES))

.PHONY: all analyze test verify clean json-adapter verify-json-dependency

# Host-buildable reconstructed modules; this is not an ESP32 flash image.
all: build/libzseries_oem.a

build/libzseries_oem.a: $(OBJECTS)
	"$(AR)" rcs $@ $^

# Concrete JSON adapter is a separately requested host archive. Obtain its
# pinned dependency with tools/acquire_arduinojson.py or set ARDUINOJSON_DIR.
json-adapter: build/libzseries_oem_json.a

verify-json-dependency:
	"$(PYTHON)" -c 'import sys; sys.path.insert(0,"tools"); from acquire_arduinojson import verify_source; verify_source(sys.argv[1])' "$(ARDUINOJSON_DIR)"

build/json_%.o: adapters/arduinojson/%.cpp $(OWNED_INCLUDES) | verify-json-dependency
	@mkdir -p build
	"$(CXX)" $(CPPFLAGS) $(CXXFLAGS) -I"$(ARDUINOJSON_DIR)/src" -c $< -o $@

build/libzseries_oem_json.a: $(OBJECTS) $(JSON_OBJECTS)
	"$(AR)" rcs $@ $^

build/%.o: src/%.cpp $(OWNED_INCLUDES)
	@mkdir -p build
	"$(CXX)" $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/%.o: src/%.c $(wildcard include/zseries/*.h)
	@mkdir -p build
	"$(CC)" $(CPPFLAGS) $(CFLAGS) -c $< -o $@

analyze:
	"$(PYTHON)" tools/analyze_images.py
	"$(PYTHON)" tools/recover_enums.py

test:
	"$(PYTHON)" -m unittest discover -s tests -p 'test_*.py'

verify: all test
	"$(PYTHON)" tools/analyze_images.py --check
	"$(PYTHON)" tools/recover_enums.py --check
	"$(PYTHON)" tools/recover_msp_images.py --check

clean:
	rm -f build/*.o build/libzseries_oem.a build/libzseries_oem_json.a
