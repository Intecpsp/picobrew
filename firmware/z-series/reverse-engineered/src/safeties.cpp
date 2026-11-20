#include "zseries/safeties.hpp"

namespace zseries { namespace oem {

void start_monitor(TimedThresholdMonitor& monitor, std::uint32_t now,
                   std::uint32_t delay, double threshold) noexcept {
    monitor.enabled = true;
    monitor.threshold = threshold;
    monitor.deadline_ms = now + delay;
}

void stop_monitor(TimedThresholdMonitor& monitor, bool& tripped) noexcept {
    if (monitor.enabled) {
        monitor.enabled = false;
        tripped = false;
    }
}

void update_monitor(const TimedThresholdMonitor& monitor, std::uint32_t now,
                    double value, bool& tripped) noexcept {
    if (monitor.enabled && now >= monitor.deadline_ms && value > monitor.threshold && !tripped)
        tripped = true;
}

bool initialize_safety_input(const SafetyIo& io) noexcept {
    if (!io.configure_input) return false;
    io.configure_input(io.context, OriginalFilterInputPin);
    return true;
}

bool update_safeties(SafetyController& controller, SafetyState& state,
                      const HeaterSensors& sensors, const z_msp_state& msp,
                      std::uint32_t now, const SafetyIo& io) noexcept {
    if (!io.read_pin) return false;
    if (!state.thermal.enabled) return true;
    state.filter_missing = io.read_pin(io.context, OriginalFilterInputPin) == 1;
    const double hex = sensors.temperatures[static_cast<std::size_t>(TempSensorId::Hex)].celsius;
    const double wort = sensors.temperatures[static_cast<std::size_t>(TempSensorId::Wort)].celsius;
    const double drain = sensors.temperatures[static_cast<std::size_t>(TempSensorId::Drain)].celsius;
    const double hex_wort = wort > hex ? wort - hex : hex - wort;
    const double wort_drain = wort > drain ? wort - drain : drain - wort;
    controller.previous_hot = state.thermal.too_hot;
    controller.previous_cold = state.thermal.too_cold;
    controller.previous_old = state.thermal.too_old;
    if ((msp.pumps & 0x10) == 0) {
        if (!controller.hex_rise.enabled) start_monitor(controller.hex_rise, now, 0, hex + 20.0);
        stop_monitor(controller.hex_wort_delta, state.max_hex_wort_delta);
        stop_monitor(controller.wort_drain_delta, state.max_wort_drain_delta);
    } else {
        stop_monitor(controller.hex_rise, state.max_hex_rise);
        if (!controller.hex_wort_delta.enabled) start_monitor(controller.hex_wort_delta, now, 5000, 15.0);
        if (static_cast<std::uint8_t>(msp.valve_position - 4) < 6 && !controller.wort_drain_delta.enabled)
            start_monitor(controller.wort_drain_delta, now, 300000, 50.0);
    }
    update_monitor(controller.hex_rise, now, hex, state.max_hex_rise);
    update_monitor(controller.hex_wort_delta, now, sensors.hex_rate < 0.0 ? 0.0 : hex_wort,
                    state.max_hex_wort_delta);
    update_monitor(controller.wort_drain_delta, now, wort_drain, state.max_wort_drain_delta);
    return true;
}

}} // namespace zseries::oem
