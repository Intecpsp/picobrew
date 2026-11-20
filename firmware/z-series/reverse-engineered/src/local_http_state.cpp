#include "zseries/local_http_state.hpp"
#include <cmath>

namespace zseries { namespace oem {
namespace {
class StateWriter {
public:
    StateWriter(char* bytes, std::size_t capacity) noexcept : bytes_(bytes), available_(capacity - 1) { bytes_[0] = 0; }
    std::size_t size() const noexcept { return used_; }
    void byte(char value) noexcept {
        if (available_) { bytes_[used_++] = value; --available_; bytes_[used_] = 0; }
    }
    void text(const char* value) noexcept { while (*value) byte(*value++); }
    void string(const char* value) noexcept {
        if (!value) { text("null"); return; }
        byte('"');
        while (*value) {
            const char next = *value++;
            const char* escapes = "\"\"\\\\b\bf\fn\nr\rt\t";
            while (*escapes && escapes[1] != next) escapes += 2;
            if (*escapes) { byte('\\'); byte(*escapes); } else byte(next);
        }
        byte('"');
    }
    void unsigned_number(std::uint32_t value) noexcept {
        char digits[10]; unsigned count = 0;
        do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
        while (count) byte(digits[--count]);
    }
    void signed_number(std::int32_t value) noexcept {
        const auto bits = static_cast<std::uint32_t>(value);
        if (value < 0) { byte('-'); unsigned_number(0u - bits); }
        else unsigned_number(bits);
    }
    void real(double value) noexcept {
        if (!std::isfinite(value)) { text("null"); return; }
        if (value < 0) { byte('-'); value = -value; }
        // Original power tables 0x4011368c/0x40113724 and normalization at
        // 0x401194b4. Powers are indexed by exponent bits, not decimal digits.
        static const double powers[] = {1e1,1e2,1e4,1e8,1e16,1e32,1e64,1e128,1e256};
        static const double inverse[] = {1e-1,1e-2,1e-4,1e-8,1e-16,1e-32,1e-64,1e-128,1e-256};
        static const double thresholds[] = {1,1e-1,1e-3,1e-7,1e-15,1e-31,1e-63,1e-127,1e-255};
        int exponent = 0;
        if (value >= 1e7) {
            for (int bit = 8; bit >= 0; --bit)
                if (value >= powers[bit]) { value *= inverse[bit]; exponent += 1 << bit; }
        } else if (value > 0 && value <= 1e-5) {
            for (int bit = 8; bit >= 0; --bit)
                if (value < thresholds[bit]) { value *= powers[bit]; exponent -= 1 << bit; }
        }
        auto integral = static_cast<std::uint32_t>(value);
        std::uint32_t divisor = 1000000000;
        unsigned places = 9;
        for (auto copy = integral; copy > 9; copy /= 10) { divisor /= 10; --places; }
        const double fraction = (value - static_cast<double>(integral)) * static_cast<double>(divisor);
        auto decimals = static_cast<std::uint32_t>(fraction);
        const double remainder = fraction - static_cast<double>(decimals);
        decimals += static_cast<std::uint32_t>(remainder + remainder);
        if (decimals >= divisor) {
            ++integral; decimals = 0;
            if (integral >= 10 && exponent != 0) { integral = 1; ++exponent; }
        }
        while (places && decimals % 10 == 0) { decimals /= 10; --places; }
        unsigned_number(integral);
        if (places) {
            char digits[9];
            for (unsigned at = places; at > 0; --at) { digits[at - 1] = static_cast<char>('0' + decimals % 10); decimals /= 10; }
            byte('.'); for (unsigned at = 0; at < places; ++at) byte(digits[at]);
        }
        if (exponent) { byte('e'); signed_number(exponent); }
    }
    void key(const char* name, bool first = false) noexcept { if (!first) byte(','); string(name); byte(':'); }
private:
    char* bytes_;
    std::size_t available_, used_ = 0;
};

unsigned location(std::uint8_t valve, bool pak) noexcept {
    switch (valve) {
    case 2: return 1;
    case 4: return 2;
    case 5: return 3;
    case 6: return pak ? 3 : 4;
    case 7: return 4;
    case 8: return 5;
    case 9: return 6;
    default: return 0;
    }
}
}

bool serialize_local_http_state(const LocalHttpState& state, char* output,
                               std::size_t capacity, std::size_t& written) noexcept {
    if (!output || capacity == 0 || state.pause_reason > 17) return false;
    StateWriter writer(output, capacity); writer.byte('{');
    writer.key("ThermoBlockTemp", true); writer.real(state.thermoblock_temperature);
    writer.key("WortTemp"); writer.real(state.wort_temperature);
    writer.key("AmbientTemp"); writer.real(state.ambient_temperature);
    writer.key("DrainTemp"); writer.real(state.drain_temperature);
    writer.key("ValvePosition"); writer.unsigned_number(state.valve_position);
    writer.key("KegPumpOn"); writer.unsigned_number((state.pumps >> 4) & 1);
    writer.key("DrainPumpOn"); writer.unsigned_number((state.pumps >> 5) & 1);
    writer.key("PauseReason"); writer.unsigned_number(state.pause_reason);
    writer.key("rssi"); writer.signed_number(state.rssi);
    writer.key("TargetTemp"); writer.real(state.target_temperature);
    writer.key("StepName"); writer.string(state.step_name);
    writer.key("SecondsEllapsed"); writer.unsigned_number(state.elapsed_seconds);
    writer.key("SecondsRemaining"); writer.unsigned_number(state.remaining_seconds);
    writer.key("UsePicoPakMode"); writer.unsigned_number(state.runtime_flags & 1);
    writer.key("Location"); writer.unsigned_number(location(state.valve_position, (state.runtime_flags & 1) != 0));
    writer.byte('}'); written = writer.size(); return true;
}

LocalHttpResult send_local_http_state(LocalHttpServer& server, const LocalHttpState& state) {
    const auto& response = server.services.response;
    if (!response.headers || !response.write) return LocalHttpResult::MissingService;
    if (state.pause_reason > 17) return LocalHttpResult::ServiceFailure;
    server.clear_buffer(); server.frozen = true;
    std::size_t size = 0;
    if (!serialize_local_http_state(state, server.buffer.data(), LocalHttpServer::BufferCapacity, size))
        return LocalHttpResult::ServiceFailure;
    if (!response.headers(response.context, 200, static_cast<std::int32_t>(size), "application/json"))
        return LocalHttpResult::ServiceFailure;
    return response.write(response.context, reinterpret_cast<const std::uint8_t*>(server.buffer.data()), size)
        ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;
}

LocalHttpState local_http_state(const HeaterSensors& sensors, const HeaterController& heater,
                               const z_msp_state& msp, const ProgramRuntime& runtime, std::int32_t rssi) noexcept {
    LocalHttpState value;
    value.thermoblock_temperature = sensors.temperatures[static_cast<unsigned>(TempSensorId::Hex)].celsius;
    value.wort_temperature = sensors.temperatures[static_cast<unsigned>(TempSensorId::Wort)].celsius;
    value.ambient_temperature = sensors.temperatures[static_cast<unsigned>(TempSensorId::Ambient)].celsius;
    value.drain_temperature = sensors.temperatures[static_cast<unsigned>(TempSensorId::Drain)].celsius;
    value.valve_position = msp.valve_position; value.pumps = msp.pumps;
    value.pause_reason = runtime.state.pause_reason; value.rssi = rssi;
    value.target_temperature = heater.state.target_celsius; value.step_name = runtime.state.step_name;
    value.elapsed_seconds = runtime.state.elapsed_seconds; value.remaining_seconds = runtime.state.total_left_seconds;
    value.runtime_flags = runtime.state.flags; return value;
}
}} // namespace zseries::oem
