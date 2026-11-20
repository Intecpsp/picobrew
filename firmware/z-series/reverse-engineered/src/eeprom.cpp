#include "zseries/eeprom.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
namespace {
std::uint32_t word(const EepromBytes& bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}
void word(EepromBytes& bytes, std::size_t offset, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
template<std::size_t N> std::size_t set_string(std::array<char, N>& destination, const char* source) noexcept {
    destination[0] = 0;
    if (!source) return 0;
    const std::size_t length = std::min(std::strlen(source), N - 1);
    std::memcpy(destination.data(), source, length);
    destination[length] = 0;
    return length; // Unused bytes after the terminator are preserved.
}
}

EepromBytes encode_eeprom(const EepromData& data) noexcept {
    EepromBytes bytes{};
    word(bytes, 0, data.version);
    bytes[4] = data.use_ethernet;
    bytes[5] = data.use_imperial;
    bytes[6] = data.show_detail;
    bytes[7] = data.slideshow_cycle_static;
    word(bytes, 8, static_cast<std::uint32_t>(data.language));
    std::memcpy(bytes.data() + 12, data.wifi_ssid.data(), data.wifi_ssid.size());
    std::memcpy(bytes.data() + 44, data.wifi_password.data(), data.wifi_password.size());
    word(bytes, 108, static_cast<std::uint32_t>(data.still_session_mode));
    word(bytes, 112, static_cast<std::uint32_t>(data.azure_enabled));
    word(bytes, 116, static_cast<std::uint32_t>(data.boiler));
    return bytes;
}
EepromData decode_eeprom(const EepromBytes& bytes) noexcept {
    EepromData data;
    data.version = word(bytes, 0);
    data.use_ethernet = bytes[4];
    data.use_imperial = bytes[5];
    data.show_detail = bytes[6];
    data.slideshow_cycle_static = bytes[7];
    data.language = static_cast<LanguageType>(word(bytes, 8));
    std::memcpy(data.wifi_ssid.data(), bytes.data() + 12, data.wifi_ssid.size());
    std::memcpy(data.wifi_password.data(), bytes.data() + 44, data.wifi_password.size());
    data.still_session_mode = static_cast<SessionMode>(word(bytes, 108));
    data.azure_enabled = static_cast<BoolOption>(word(bytes, 112));
    data.boiler = static_cast<BoilerType>(word(bytes, 116));
    return data;
}

// 0x400d6410. Raw boolean bytes and string contents are not normalized here.
bool eeprom_enforce_valid_enums(EepromData& data) noexcept {
    bool changed = false;
    if (static_cast<std::uint32_t>(data.language) > 2) { data.language = LanguageType::English; changed = true; }
    if (static_cast<std::uint32_t>(data.still_session_mode) > 2) { data.still_session_mode = SessionMode::Unknown; changed = true; }
    if (static_cast<std::uint32_t>(data.azure_enabled) > 2) { data.azure_enabled = BoolOption::Unknown; changed = true; }
    if (static_cast<std::uint32_t>(data.boiler) > 1) { data.boiler = BoilerType::Big; changed = true; }
    return changed;
}

// 0x401bf7f0. Language/session/Azure are deliberately not checked by this predicate.
bool eeprom_data_valid(const EepromData& data) noexcept {
    return data.version == 0 &&
        std::find(data.wifi_ssid.begin(), data.wifi_ssid.end(), '\0') != data.wifi_ssid.end() &&
        std::find(data.wifi_password.begin(), data.wifi_password.end(), '\0') != data.wifi_password.end() &&
        static_cast<std::uint32_t>(data.boiler) < 2;
}

// 0x400d0ed4: case sensitive, no whitespace trimming; an empty string is true.
bool eeprom_text_boolean(const char* text) noexcept {
    return text && std::strcmp(text, "0") != 0 && std::strcmp(text, "off") != 0 && std::strcmp(text, "false") != 0;
}
std::size_t eeprom_set_ssid(EepromData& data, const char* text) noexcept { return set_string(data.wifi_ssid, text); }
std::size_t eeprom_set_password(EepromData& data, const char* text) noexcept { return set_string(data.wifi_password, text); }

const std::array<EepromCommandDescriptor, 9>& eeprom_commands() noexcept {
    static const std::array<EepromCommandDescriptor, 9> commands = {{
        {EepromCommand::Info, true, "i", "Show EEPROM info"},
        {EepromCommand::Ssid, false, "ssid ", "Sets WiFi SSID"},
        {EepromCommand::Password, false, "pass ", "Sets WiFi password"},
        {EepromCommand::Azure, false, "azure ", "Sets AzureEnabled boolean"},
        {EepromCommand::Ethernet, false, "eth ", "Sets useEthernet boolean"},
        {EepromCommand::BoilerUnknown, true, "boiler u", "Sets Boiler type to Unknown"},
        {EepromCommand::BoilerBig, true, "boiler b", "Sets Boiler type to Big"},
        {EepromCommand::BoilerSmall, true, "boiler s", "Sets Boiler type to Small"},
        {EepromCommand::Test, true, "test", "Run EEPPROM unit test"}
    }};
    return commands;
}

// 0x400fbc1c. As in the OEM, begin/commit status does not veto later work; it is
// exposed for inspection instead of silently changing the reconstructed flow.
EepromResult EepromStore::setup() {
    assertion_line = 0;
    if (!backend.begin || !backend.read_data || !backend.write_data || !backend.commit)
        return EepromResult::MissingBackend;
    last_begin_succeeded = backend.begin(backend.context, requested_bytes);
    if (requested_bytes < EepromDataSize) { assertion_line = 0x137; return EepromResult::BufferTooSmall; }
    EepromBytes bytes = encode_eeprom(data);
    backend.read_data(backend.context, bytes);
    data = decode_eeprom(bytes);
    initialized = true;
    if (eeprom_enforce_valid_enums(data)) {
        const EepromResult result = write();
        if (result != EepromResult::Ok) return result;
    }
    if (!eeprom_data_valid(data)) {
        const EepromResult result = reset();
        if (result != EepromResult::Ok) return result;
    }
    if (!services.register_commands) return EepromResult::MissingRegistry;
    services.register_commands(services.context, *this, eeprom_commands());
    return EepromResult::Ok;
}
EepromResult EepromStore::write() {
    if (!initialized) { assertion_line = 0x160; return EepromResult::NotInitialized; }
    if (!backend.write_data || !backend.commit) return EepromResult::MissingBackend;
    backend.write_data(backend.context, encode_eeprom(data));
    last_commit_succeeded = backend.commit(backend.context);
    return EepromResult::Ok;
}
EepromResult EepromStore::reset() {
    if (!initialized) { assertion_line = 0x158; return EepromResult::NotInitialized; }
    const BoilerType boiler = static_cast<std::uint32_t>(data.boiler) < 2 ? data.boiler : BoilerType::Big;
    data = EepromData{};
    data.boiler = boiler;
    return write();
}
EepromResult EepromStore::get_data(EepromData*& result) noexcept {
    result = nullptr;
    if (!initialized) { assertion_line = 0x130; return EepromResult::NotInitialized; }
    result = &data;
    return EepromResult::Ok;
}

EepromResult EepromStore::command(EepromCommand id, const char* argument) {
    switch (id) {
        case EepromCommand::Info:
            if (!services.report_data) return EepromResult::MissingReporter;
            services.report_data(services.context, data); return EepromResult::Ok;
        case EepromCommand::Ssid: (void)eeprom_set_ssid(data, argument); return EepromResult::Ok;
        case EepromCommand::Password: (void)eeprom_set_password(data, argument); return EepromResult::Ok;
        case EepromCommand::Azure: data.azure_enabled = eeprom_text_boolean(argument) ? BoolOption::True : BoolOption::False; return EepromResult::Ok;
        case EepromCommand::Ethernet: data.use_ethernet = eeprom_text_boolean(argument); return EepromResult::Ok;
        case EepromCommand::BoilerUnknown: data.boiler = BoilerType::Unknown; return EepromResult::Ok;
        case EepromCommand::BoilerBig: data.boiler = BoilerType::Big; return EepromResult::Ok;
        case EepromCommand::BoilerSmall: data.boiler = BoilerType::Small; return EepromResult::Ok;
        case EepromCommand::Test: return eeprom_original_self_test() ? EepromResult::Ok : EepromResult::SelfTestFailed;
    }
    return EepromResult::InvalidCommand;
}

// The data-focused embedded checks at 0x400dd2e0, without its diagnostic harness.
bool eeprom_original_self_test() noexcept {
    EepromData data;
    if (eeprom_enforce_valid_enums(data) || !eeprom_data_valid(data)) return false;
    data.language = static_cast<LanguageType>(0xffffffffu);
    data.still_session_mode = static_cast<SessionMode>(0xffffffffu);
    data.azure_enabled = static_cast<BoolOption>(0xffffffffu);
    data.boiler = static_cast<BoilerType>(0xffffffffu);
    if (!eeprom_enforce_valid_enums(data) || eeprom_enforce_valid_enums(data)) return false;
    EepromBytes erased; erased.fill(0xff); data = decode_eeprom(erased);
    if (eeprom_data_valid(data)) return false;
    data.version = 0;
    if (eeprom_data_valid(data)) return false;
    data.wifi_ssid[0] = 0;
    if (eeprom_data_valid(data)) return false;
    data.wifi_password[0] = 0;
    if (eeprom_data_valid(data)) return false;
    data.boiler = BoilerType::Big;
    if (!eeprom_data_valid(data)) return false;
    data.boiler = BoilerType::Small;
    if (!eeprom_data_valid(data)) return false;
    data.boiler = BoilerType::Unknown;
    if (eeprom_data_valid(data)) return false;
    data.boiler = static_cast<BoilerType>(9999);
    return !eeprom_data_valid(data);
}
}} // namespace zseries::oem
