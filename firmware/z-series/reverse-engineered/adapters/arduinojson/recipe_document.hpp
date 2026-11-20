// Reuse the selected ArduinoJson dependency; do not vendor its parser here.
#pragma once
#define ARDUINOJSON_USE_DOUBLE 1
#define ARDUINOJSON_USE_LONG_LONG 0
#define ARDUINOJSON_DECODE_UNICODE 0
#define ARDUINOJSON_ENABLE_NAN 0
#define ARDUINOJSON_ENABLE_INFINITY 0
#define ARDUINOJSON_ENABLE_COMMENTS 1
#include <ArduinoJson.h>
#include "zseries/program_recipe_json.hpp"

namespace zseries { namespace oem { namespace json_adapter {
// The original zero-copy pool allocates one 16-byte slot per array element or
// object member (0x401bf724). Host pointers can be wider; express the same slot
// count so host acceptance tests do not accidentally halve the recipe limit.
constexpr std::size_t local_capacity = JSON_ARRAY_SIZE(64);
constexpr std::size_t http_capacity = JSON_ARRAY_SIZE(256);
static_assert(sizeof(void*) != 4 || local_capacity == 1024,
              "32-bit local JSON pool must match the original 1024 bytes");
static_assert(sizeof(void*) != 4 || http_capacity == 4096,
              "32-bit HTTP JSON pool must match the original 4096 bytes");

inline RecipeJsonError translate(ArduinoJson::DeserializationError error) noexcept {
    switch (error.code()) {
    case ArduinoJson::DeserializationError::Ok: return RecipeJsonError::Ok;
    case ArduinoJson::DeserializationError::EmptyInput:
    case ArduinoJson::DeserializationError::IncompleteInput: return RecipeJsonError::IncompleteInput;
    case ArduinoJson::DeserializationError::InvalidInput: return RecipeJsonError::InvalidInput;
    case ArduinoJson::DeserializationError::NoMemory: return RecipeJsonError::NoMemory;
    case ArduinoJson::DeserializationError::TooDeep: return RecipeJsonError::TooDeep;
    }
    return RecipeJsonError::InvalidInput;
}

// Original 0x40113338 rejects an active \u escape before decoding it. This
// lexical feature gate is deliberately separate from ArduinoJson's parser.
inline std::size_t legacy_escapes(char* input, std::size_t length, bool& unsupported) noexcept {
    char quote = 0, comment = 0;
    std::size_t written = 0;
    unsupported = false;
    for (std::size_t index = 0; index < length && input[index]; ++index) {
        const char value = input[index];
        const char next = index + 1 < length ? input[index + 1] : 0;
        if (comment) {
            input[written++] = value;
            if (comment == '/' && value == '\n') comment = 0;
            else if (comment == '*' && value == '*' && next == '/') {
                input[written++] = next; ++index; comment = 0;
            }
        } else if (!quote) {
            input[written++] = value;
            if (value == '"' || value == '\'') quote = value;
            else if (value == '/' && (next == '/' || next == '*')) {
                input[written++] = next; ++index; comment = next;
            }
        } else if (value == '\\' && next) {
            if (next == 'u') { unsupported = true; return written; }
            // 0x400d1638 decodes b/f/n/r/t and passes every other escaped
            // character through. Preserve the usual escapes for ArduinoJson;
            // remove only a legacy escape's otherwise rejected backslash.
            if (next == 'b' || next == 'f' || next == 'n' || next == 'r' ||
                next == 't' || next == '\\' || next == '/' || next == '"' || next == '\'')
                input[written++] = value;
            input[written++] = next; ++index;
        } else {
            input[written++] = value;
            if (value == quote) quote = 0;
        }
    }
    return written;
}

inline RecipeJsonError decode(ArduinoJson::JsonDocument& document, char* input,
        std::size_t length, std::uint8_t nesting = 10) noexcept {
    if (!input) return RecipeJsonError::IncompleteInput;
    bool unsupported = false;
    const std::size_t prefix = legacy_escapes(input, length, unsupported);
    const RecipeJsonError result = translate(ArduinoJson::deserializeJson(document, input, prefix,
        ArduinoJson::DeserializationOption::NestingLimit(nesting)));
    // Parsing the prefix retains an earlier syntax/capacity/depth error. Only
    // the incomplete quoted string at the gated escape becomes NotSupported.
    if (unsupported && result == RecipeJsonError::IncompleteInput) return RecipeJsonError::NotSupported;
    return result;
}

// 0x401142bc uses unsigned conversion after negating an integer parsed from a
// string. A JSON numeric value and the corresponding numeric string therefore
// differ for negative integers; retain that application conversion explicitly.
inline double number(ArduinoJson::JsonVariantConst value) noexcept {
    const char* text = value.as<const char*>();
    if (text && text[0] == '-' && text[1]) {
        std::uint32_t magnitude = 0;
        bool integer = true;
        for (const char* p = text + 1; *p; ++p) {
            if (*p < '0' || *p > '9') { integer = false; break; }
            const auto digit = static_cast<std::uint32_t>(*p - '0');
            if (magnitude > (0xffffffffu - digit) / 10u) { integer = false; break; }
            magnitude = magnitude * 10u + digit;
        }
        if (integer) return static_cast<double>(0u - magnitude);
    }
    return value.as<double>();
}
}}} // namespace zseries::oem::json_adapter
