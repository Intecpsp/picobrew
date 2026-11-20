// Original IPicoStill.h sensor records, sample windows and statistics.
#pragma once
#include "zseries/console.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace zseries { namespace oem {
struct PicoStillSensors {
    std::uint32_t timestamp = 0;
    std::array<double, 4> temperature{};
    double pressure = 0;
    bool pump_running = false, fan_running = false, program_running = false;
    std::array<char, 65> error{};
    std::uint32_t delta_ms = 0;
    std::array<double, 5> speed{};
    bool has_data() const noexcept { return timestamp != 0; }
    bool has_speed_data() const noexcept { return timestamp != 0 && delta_ms != 0; }
    // Original 0x400d5730; elapsed time is the original unsigned 32-bit delta.
    void update_speeds(const PicoStillSensors&, const ConsoleServices* diagnostics = nullptr,
                       ConsoleResult* result = nullptr) noexcept;
};
struct PicoStillStatistics {
    std::array<double, 30> values{};
    std::uint32_t count = 0, next = 0;
    double sum = 0, mean = 0, squared_deviations = 0, difference = 0;
    double difference_alpha = 0.05, smoothed_difference = 0;
    bool difference_defined = false;
    // Original 0x400ef818 and its exponential-filter helper 0x400d0df8.
    void update(double value) noexcept;
};
struct PicoStillSamples {
    PicoStillSensors current, seconds10, seconds60;
    PicoStillStatistics t1, t2, pressure;
    std::uint32_t assertion_line = 0;
    ConsoleResult diagnostics_result = ConsoleResult::Ok;
    // Original 0x401009bc. Failed parsing clears time/delta/error while retaining
    // the preceding numeric sample, exactly as in the recovered instructions.
    // Reads at most length bytes; direct callers may supply a nonterminated span.
    // An embedded NUL retains the original CString logical end.
    bool parse(std::uint32_t now, const char* body, std::size_t length,
               const ConsoleServices* diagnostics = nullptr) noexcept;
    // Original 0x400ef9cc and 0x400db708; retains the original strict deadlines.
    void update_statistics(std::uint32_t now, const ConsoleServices* diagnostics = nullptr) noexcept;
};
}} // namespace zseries::oem
