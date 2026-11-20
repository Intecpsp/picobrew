#include "picoferm/core.hpp"
#include <cstring>
#include <cmath>
#include <limits>
#include <string>

namespace picoferm { namespace oem {
namespace {
float as_float(std::uint32_t word) noexcept { float value; std::memcpy(&value, &word, 4); return value; }
std::uint32_t bits(float value) noexcept { std::uint32_t word; std::memcpy(&word, &value, 4); return word; }
std::int32_t signed_word(std::uint32_t word) noexcept { std::int32_t value; std::memcpy(&value, &word, 4); return value; }
std::uint32_t positive_atoi(const char* value) noexcept {
    // Session tokens contain only decimal digits before this call. Match the
    // image's 32-bit newlib strtol saturation, including arbitrarily long input.
    std::uint32_t result = 0;
    while (*value >= '0' && *value <= '9') {
        const unsigned digit = static_cast<unsigned>(*value++ - '0');
        if (result > (0x7fffffffu - digit) / 10u) result = 0x7fffffffu;
        else result = result * 10u + digit;
    }
    return result;
}
}

void diagnostic_assertion(Ports& ports) { ports.yield(); ports.yield(); }
void diagnostic_warning(Ports& ports) { ports.yield(); ports.yield(); }
void Ports::masked_text(const char* scope,const char* key,const char* value) {
    const std::string masked(value?std::strlen(value):0,'*');text(scope,key,masked.c_str());
}

bool parse_session(Session& session, const char* response, Ports& ports) {
    session.state = SessionState::Undefined; session.error_flags = 0;
    if (!response) { diagnostic_assertion(ports); return false; }
    bool comma = false;
    std::uint32_t code = 0;
    const char* token = response;
    const char* cursor = response;
    for (;; ++cursor) {
        if (*cursor == '\0') {
            if (cursor != token) {
                if (comma) session.error_flags = positive_atoi(token);
                else code = positive_atoi(token);
            } else if (!comma) return true;
            break;
        }
        if (*cursor == ',') { code = positive_atoi(token); comma = true; token = cursor + 1; }
        else if (*cursor < '0' || *cursor > '9') {
            diagnostic_warning(ports);
            if (!comma) return true;
            break;
        }
    }
    if ((code & 0xfffffff7u) != 2u) diagnostic_warning(ports);
    else if (code == 2) session.state = SessionState::Idle;
    else if (code == 10) session.state = SessionState::Fermenting;
    return true;
}

std::uint32_t PersistentData::word(std::size_t offset) const noexcept {
    return std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset+1]) << 8 |
           std::uint32_t(bytes[offset+2]) << 16 | std::uint32_t(bytes[offset+3]) << 24;
}
void PersistentData::word(std::size_t offset, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i) bytes[offset+i] = static_cast<std::uint8_t>(value >> (8*i));
}
void PersistentData::migrate(Ports& ports) {
    word(cookie_position, 0xdeadbeefu); word(version_position, 1); bytes[log_start_position] = 0;
    if (bytes[log_count_position] != 4) return;
    bool invalid = true;
    for (unsigned i = 0; i < 8; ++i) {
        const float value = as_float(word(logs_position + 4*i));
        // OEM soft-float comparison treats unordered as a failed <= test.
        if (!(std::fabs(value) <= 0.01f)) { invalid = false; break; }
    }
    ports.boolean("EPRM", "hasInvalidData", invalid);
    if (invalid) bytes[log_count_position] = 0;
}
void PersistentData::reset(Ports& ports) {
    bytes.fill(0); word(slope_position, 110840); migrate(ports);
}
CalibrationStatus PersistentData::calibration(Ports& ports) const {
    if (word(slope_position) - 90000u > 60000u) diagnostic_assertion(ports);
    CalibrationStatus result;
    if (!bytes[has_offset_position]) {
        result.error = result.can_calibrate = true; result.message = "Pressure offset is missing.";
    } else {
        const float offset = static_cast<float>(word(pressure_offset_position));
        if (offset < 34470.0f || offset > 234000.0f) {
            result.error = result.can_calibrate = true; result.message = "Pressure offset is out of range.";
        }
    }
    return result;
}
void PersistentData::sanitize(Ports& ports) {
    bool blank = true;
    for (auto byte : bytes) if (byte != 255) { blank = false; break; }
    ports.boolean("EPRM", "blank", blank);
    if (blank) reset(ports);
    if (word(cookie_position) != 0xdeadbeefu || word(version_position) == 0) {
        ports.text("EPRM", "Migrate", "0 -> 1"); migrate(ports);
    }
    if (word(slope_position) - 90000u > 60000u) {
        diagnostic_warning(ports); ports.integer("EPRM", "PressureSlope", word(slope_position));
        word(slope_position, 110840);
    }
    const CalibrationStatus status = calibration(ports);
    if (status.error) ports.text("EPRM", "PressureError", status.message);
}
void PersistentData::append(Measurement measurement, Ports& ports) {
    if (bytes[log_start_position] > 11) diagnostic_assertion(ports);
    const unsigned start = bytes[log_start_position], count = bytes[log_count_position];
    const unsigned index = (start + count) % log_capacity;
    if (count < log_capacity) ++bytes[log_count_position];
    else {
        if (index != start) diagnostic_assertion(ports);
        bytes[log_start_position] = static_cast<std::uint8_t>((start+1) % log_capacity);
    }
    word(logs_position + 8*index, bits(measurement.temperature));
    word(logs_position + 8*index + 4, bits(measurement.pressure));
}
bool PersistentData::measurement(std::size_t index, Measurement& result) const noexcept {
    if (index >= log_capacity) return false;
    result.temperature = as_float(word(logs_position + 8*index));
    result.pressure = as_float(word(logs_position + 8*index + 4)); return true;
}
bool LogIterator::done() const noexcept { return processed >= data.bytes[PersistentData::log_count_position] || processed > 11; }
void LogIterator::advance() noexcept { if (!done()) { ++processed; index = (index + 1u) % 12u; } }

std::uint8_t read_button(Ports& ports) {
    ports.digital_write(15, false); const std::uint32_t result = ports.digital_read(13); ports.digital_write(15, true);
    if (result > 127) diagnostic_assertion(ports);
    return static_cast<std::uint8_t>(result);
}
void Measurements::persist(Ports& ports) {
    if (session_state == SessionState::Idle) data.bytes[0x62] = 2;
    else if (session_state == SessionState::Fermenting) data.bytes[0x62] = 10;
    data.bytes[0x63] = read_button(ports); ports.persist(data.bytes.data(), data.bytes.size());
}
void Measurements::on_adc(float temperature, std::uint32_t pressure, Ports& ports) {
    const std::int32_t slope = signed_word(data.word(PersistentData::slope_position));
    if (slope < 1) diagnostic_assertion(ports);
    ports.text("MAIN", "CB", "HandleAdcDataEvent");
    ports.number("ADC", "Temperature", temperature); ports.integer("ADC", "Pressure", pressure);
    if (machine_state == 8) {
        data.word(PersistentData::pressure_offset_position, pressure); data.bytes[PersistentData::has_offset_position] = 1;
        persist(ports); return;
    }
    const std::uint32_t offset = data.word(PersistentData::pressure_offset_position);
    if (!offset || !data.bytes[PersistentData::has_offset_position]) {
        ports.text("MAIN", "ADC", "Pressure offset missing or invalid"); return;
    }
    const float difference = pressure >= offset ? static_cast<float>(pressure - offset) : 0.0f;
    const float scaled = difference * 10.0f;
    // The original libgcc division returns positive canonical NaN for 0/0;
    // x86 hosts commonly return its negative encoding. Preserve stored bytes.
    const float delta = slope == 0 && scaled == 0.0f ? as_float(0x7fc00000u) : scaled / static_cast<float>(slope);
    ports.number("MAIN", "DeltaPressure", delta);
    if (machine_state == 4) data.append({temperature, delta}, ports);
}
}} // namespace picoferm::oem
