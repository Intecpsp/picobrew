#include "zseries/pico_still_sensors.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace zseries { namespace oem {
void PicoStillSensors::update_speeds(const PicoStillSensors& previous) noexcept {
    const std::uint32_t elapsed = timestamp - previous.timestamp;
    const double seconds = static_cast<double>(elapsed) / 1000.0;
    if (timestamp == 0 && seconds <= 0.0) { delta_ms = 0; return; }
    delta_ms = elapsed;
    for (std::size_t i = 0; i < temperature.size(); ++i) speed[i] = ((temperature[i] - previous.temperature[i]) * 60.0) / seconds;
    speed[4] = ((pressure - previous.pressure) * 60.0) / seconds;
}
void PicoStillStatistics::update(double value) noexcept {
    if (count < values.size()) {
        const double old_mean = mean;
        ++count; sum = value + sum; mean = sum / static_cast<double>(count);
        if (count != 1) squared_deviations = squared_deviations + (value - old_mean) * (value - mean);
        else squared_deviations = 0;
    } else {
        const double removed = values[next], old_mean = mean, delta = value - removed;
        sum = delta + sum; mean = sum / static_cast<double>(count);
        squared_deviations = (((value + removed) - old_mean) - mean) * delta + squared_deviations;
        if (squared_deviations < 0) squared_deviations = 0;
    }
    const double removed = count > 1 ? values[next] : 0;
    values[next] = value; difference = value - removed;
    if (!difference_defined) { difference_defined = true; smoothed_difference = difference; }
    else smoothed_difference = (1.0 - difference_alpha) * smoothed_difference + difference * difference_alpha;
    if (++next >= values.size()) next = 0;
}
namespace {
struct Tokens {
    const char* next; const char* end; const char* current = nullptr; std::size_t length = 0;
    Tokens(const char* text,const char* limit) noexcept : next(text),end(limit) {}
    bool advance(std::uint32_t& assertion) noexcept {
        current = next;
        if (!next || next >= end) { current = next = nullptr; length = 0; return false; }
        const char* comma = static_cast<const char*>(std::memchr(next, ',', static_cast<std::size_t>(end-next)));
        if (!comma) { length = static_cast<std::size_t>(end-next); next = nullptr; }
        else { if (comma <= current) assertion = 0x176; length = static_cast<std::size_t>(comma - current); next = comma + 1; }
        return true;
    }
};
void copy_error(std::array<char,65>& out, const char* text,std::size_t length) noexcept {
    out.fill(0); if (text) std::memcpy(out.data(),text,std::min(length,out.size()-1));
}
}
bool PicoStillSamples::parse(std::uint32_t now, const char* body, std::size_t length) noexcept {
    const PicoStillSensors previous = current;
    current.timestamp = 0; current.delta_ms = 0; current.error.fill(0);
    if (!body || length < 3 || body[0] != '#' || body[length-1] != '#') return false;
    const char* terminator = static_cast<const char*>(std::memchr(body, 0, length));
    Tokens tokens(body+1,terminator?terminator:body+length); std::array<double,5> numbers{}; std::array<bool,3> flags{};
    std::size_t count = 0;
    while (tokens.advance(assertion_line)) {
        if (count == numbers.size()) break;
        if (tokens.length >= 16) { assertion_line = 0x19d; return false; }
        std::array<char,17> value{};std::memcpy(value.data(),tokens.current,tokens.length);
        const double parsed = std::strtod(value.data(),nullptr);
        // Original startsWith arguments are ("0.0", token). Thus "0" and "0."
        // are accepted zero spellings; "0.00" is rejected.
        if (parsed == 0.0 && std::strncmp("0.0",value.data(),tokens.length) != 0) return false;
        numbers[count++] = parsed;
    }
    if (count != numbers.size()) return false;
    count = 0;
    while (tokens.current && tokens.current < tokens.end) {
        if (count == flags.size()) {
            copy_error(current.error,tokens.current,static_cast<std::size_t>(tokens.end-tokens.current));
            const std::size_t error_length = std::strlen(current.error.data());
            if (!error_length) { assertion_line = 0x2e; return false; }
            if (current.error[error_length-1] != '#') return false;
            current.error[error_length-1] = 0;
            if (std::strcmp(current.error.data(),"None") == 0) current.error.fill(0);
            current.timestamp = now;
            for (std::size_t i=0;i<4;++i) current.temperature[i] = (numbers[i] - 32.0) / 1.8;
            current.pressure = numbers[4]; current.pump_running = flags[0];current.fan_running = flags[1];current.program_running = flags[2];
            current.update_speeds(previous);return true;
        }
        if (tokens.length >= 16) { assertion_line = 0x1b4; return false; }
        flags[count++] = tokens.length != 0 && *tokens.current != '0';
        tokens.advance(assertion_line);
    }
    return false;
}
void PicoStillSamples::update_statistics(std::uint32_t now) noexcept {
    auto update = [&](PicoStillSensors& stored, std::uint32_t seconds) {
        if (!current.has_data()) assertion_line = 0x14b;
        if (current.timestamp != now) assertion_line = 0x14c;
        if (!stored.has_data()) stored = current;
        else if (stored.timestamp + seconds*1000u < now) { const auto previous=stored;stored=current;stored.update_speeds(previous); }
    };
    update(seconds10,10); update(seconds60,60);
    t1.update(current.temperature[0]);t2.update(current.temperature[1]);pressure.update(current.pressure);
}
}} // namespace zseries::oem
