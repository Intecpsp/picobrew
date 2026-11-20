// Original Z119 EepromData / Eeprom lifecycle, with the SDK storage boundary explicit.
#pragma once
#include "zseries/enums.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {
constexpr std::size_t EepromDataSize = 120;
constexpr std::uint32_t OriginalEepromBufferSize = 512;
using EepromBytes = std::array<std::uint8_t, EepromDataSize>;

struct EepromData {
    std::uint32_t version = 0;
    std::uint8_t use_ethernet = 0;
    std::uint8_t use_imperial = 0;
    std::uint8_t show_detail = 0;
    std::uint8_t slideshow_cycle_static = 0;
    LanguageType language = LanguageType::English;
    std::array<char, 32> wifi_ssid{};
    std::array<char, 64> wifi_password{};
    SessionMode still_session_mode = SessionMode::Unknown;
    BoolOption azure_enabled = BoolOption::Unknown;
    BoilerType boiler = BoilerType::Big;
};

EepromBytes encode_eeprom(const EepromData&) noexcept;
EepromData decode_eeprom(const EepromBytes&) noexcept;
bool eeprom_enforce_valid_enums(EepromData&) noexcept;
bool eeprom_data_valid(const EepromData&) noexcept;
bool eeprom_text_boolean(const char*) noexcept;
std::size_t eeprom_set_ssid(EepromData&, const char*) noexcept;
std::size_t eeprom_set_password(EepromData&, const char*) noexcept;
bool eeprom_original_self_test() noexcept;

enum class EepromCommand { Info, Ssid, Password, Azure, Ethernet, BoilerUnknown, BoilerBig, BoilerSmall, Test };
struct EepromCommandDescriptor {
    EepromCommand id;
    bool exact_match;
    const char* token;
    const char* description;
};
const std::array<EepromCommandDescriptor, 9>& eeprom_commands() noexcept;

struct EepromBackend {
    void* context = nullptr;
    bool (*begin)(void*, std::uint32_t requested_bytes) = nullptr;
    // Match Arduino EEPROM.get/put: when its actual buffer is too small, leave
    // the destination/cache untouched. Writes mark the valid cache dirty.
    void (*read_data)(void*, EepromBytes&) = nullptr;
    void (*write_data)(void*, const EepromBytes&) = nullptr;
    bool (*commit)(void*) = nullptr;
};

// Reuses the installed Arduino EEPROM library. It does not reimplement its
// partition discovery, cache allocation, erase, or flash-write operations.
template<class ArduinoEeprom> EepromBackend arduino_eeprom_backend(ArduinoEeprom& instance) noexcept {
    EepromBackend backend;
    backend.context = &instance;
    backend.begin = [](void* context, std::uint32_t bytes) {
        return static_cast<ArduinoEeprom*>(context)->begin(bytes);
    };
    backend.read_data = [](void* context, EepromBytes& bytes) {
        static_cast<ArduinoEeprom*>(context)->get(0, bytes);
    };
    backend.write_data = [](void* context, const EepromBytes& bytes) {
        static_cast<ArduinoEeprom*>(context)->put(0, bytes);
    };
    backend.commit = [](void* context) { return static_cast<ArduinoEeprom*>(context)->commit(); };
    return backend;
}

enum class EepromResult { Ok, MissingBackend, NotInitialized, BufferTooSmall, MissingRegistry, MissingReporter, InvalidCommand, SelfTestFailed };
struct EepromStore;
struct EepromServices {
    void* context = nullptr;
    void (*register_commands)(void*, EepromStore&, const std::array<EepromCommandDescriptor, 9>&) = nullptr;
    void (*report_data)(void*, const EepromData&) = nullptr;
};

struct EepromStore {
    EepromData data;
    EepromBackend backend;
    EepromServices services;
    std::uint32_t requested_bytes = OriginalEepromBufferSize;
    bool initialized = false;
    bool last_begin_succeeded = false;
    bool last_commit_succeeded = false;
    std::uint32_t assertion_line = 0;

    EepromResult setup();
    EepromResult write();
    EepromResult reset();
    EepromResult get_data(EepromData*& result) noexcept;
    EepromResult command(EepromCommand, const char* argument = nullptr);
};
}} // namespace zseries::oem
