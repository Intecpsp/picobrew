// Original Z119 Safeties.h timed interlocks and filter input.
#pragma once
#include "zseries/heater.hpp"
#include "zseries/msp_protocol.h"
#include <cstdint>

namespace zseries { namespace oem {
constexpr std::uint8_t OriginalFilterInputPin = 36;

struct TimedThresholdMonitor {
    bool enabled = false;
    std::uint32_t deadline_ms = 0;
    double threshold = 0;
};

void start_monitor(TimedThresholdMonitor&, std::uint32_t now_ms,
                   std::uint32_t delay_ms, double threshold) noexcept;
void stop_monitor(TimedThresholdMonitor&, bool& tripped) noexcept;
void update_monitor(const TimedThresholdMonitor&, std::uint32_t now_ms,
                    double value, bool& tripped) noexcept;

struct SafetyState {
    ThermalSafety thermal;
    bool max_hex_wort_delta = false;
    bool max_wort_drain_delta = false;
    bool max_hex_rise = false;
    bool filter_missing = false;
    SafetyState() { thermal.enabled = true; }
};

struct SafetyController {
    TimedThresholdMonitor hex_rise;
    TimedThresholdMonitor hex_wort_delta;
    TimedThresholdMonitor wort_drain_delta;
    std::uint8_t previous_hot = 0;
    std::uint8_t previous_cold = 0;
    std::uint8_t previous_old = 0;
};

struct SafetyIo {
    void* context;
    void (*configure_input)(void*, std::uint8_t);
    int (*read_pin)(void*, std::uint8_t);
};

bool initialize_safety_input(const SafetyIo&) noexcept;

// Original 0x400d9510. This layer uses the same enabled flag that controls the
// heater timer's thermal refresh. Tripped monitor flags are latched until an
// enabled monitor is stopped. Deadline tests retain original unsigned >=
// comparison, including its behavior across an addition rollover.
bool update_safeties(SafetyController&, SafetyState&, const HeaterSensors&,
                      const z_msp_state&, std::uint32_t now_ms,
                      const SafetyIo&) noexcept;

}} // namespace zseries::oem
