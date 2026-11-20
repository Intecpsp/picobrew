#include "zseries/heater.hpp"
#include <cmath>
#include <limits>

namespace zseries { namespace oem {
namespace {

double bounded(double value, double minimum, double maximum) noexcept {
    if (value > maximum) return maximum;
    if (value < minimum) return minimum;
    return value;
}

void refresh_safety(const HeaterSensors& sensors, ThermalSafety& state,
                    const ThermalLimits& limits, std::uint32_t now) noexcept {
    state.too_hot = state.too_cold = state.too_old = 0;
    for (std::size_t index = 0; index < sensors.temperatures.size(); ++index) {
        const auto& sample = sensors.temperatures[index];
        const auto& limit = limits[index];
        const auto bit = static_cast<std::uint8_t>(1U << index);
        if (sample.celsius > limit.maximum_celsius) state.too_hot |= bit;
        if (sample.celsius < limit.minimum_celsius) state.too_cold |= bit;
        const std::uint32_t age = sample.updated_ms == 0 ?
            std::numeric_limits<std::uint32_t>::max() : now - sample.updated_ms;
        if (age > limit.maximum_age_ms) state.too_old |= bit;
    }
}

} // namespace

const ThermalLimits& original_thermal_limits() noexcept {
    // Initialized DRAM 0x3ffbdc48 (ages) and 0x3ffbdc58 (min/max pairs).
    static const ThermalLimits limits = {{{0.0, 110.0, 4000U}, {0.0, 105.0, 4000U},
        {0.0, 105.0, 0xffffffffU}, {-20.0, 62.0, 0xffffffffU}}};
    return limits;
}

bool pid_set_tunings(PidState& pid, double kp, double ki, double kd) noexcept {
    // The finite/sample-time guards are explicit owned API boundaries.
    if (!std::isfinite(kp) || !std::isfinite(ki) || !std::isfinite(kd) ||
            kp < 0.0 || ki < 0.0 || kd < 0.0 || pid.sample_time_ms == 0 ||
            pid.sample_time_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) return false;
    pid.display_kp = kp; pid.display_ki = ki; pid.display_kd = kd;
    const double seconds = static_cast<double>(pid.sample_time_ms) / 1000.0;
    pid.kp = kp; pid.ki = ki * seconds; pid.kd = kd / seconds;
    if (pid.reverse) { pid.kp = 0.0 - pid.kp; pid.ki = 0.0 - pid.ki; pid.kd = 0.0 - pid.kd; }
    return true;
}

void pid_initialize(PidState& pid, double input) noexcept {
    // 0x400d0050 preserves the accumulated integral term.
    pid.previous_input = input;
    pid.error = pid.proportional = pid.derivative = 0.0;
}

void pid_set_mode(PidState& pid, bool automatic, double input, double& output) noexcept {
    // 0x400d0240 initializes on either mode transition, not just manual->auto.
    if (automatic != pid.automatic) pid_initialize(pid, input);
    if (pid.automatic && !automatic) output = pid.output_minimum;
    pid.automatic = automatic;
}

bool pid_set_output_limits(PidState& pid, double minimum, double maximum, double& output) noexcept {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum) return false;
    pid.output_minimum = minimum; pid.output_maximum = maximum;
    if (pid.automatic) {
        output = bounded(output, minimum, maximum);
        pid.integral = bounded(pid.integral, minimum, maximum);
    }
    return true;
}

void pid_set_direction(PidState& pid, bool reverse) noexcept {
    if (pid.automatic && pid.reverse != reverse) {
        pid.kp = 0.0 - pid.kp; pid.ki = 0.0 - pid.ki; pid.kd = 0.0 - pid.kd;
    }
    pid.reverse = reverse;
}

void pid_compute(PidState& pid, double input, double target, double& output) noexcept {
    if (!pid.automatic) return;
    pid.error = target - input;
    pid.proportional = pid.error * pid.kp;
    pid.integral = bounded(pid.error * pid.ki + pid.integral,
                           pid.output_minimum, pid.output_maximum);
    pid.derivative = (input - pid.previous_input) * pid.kd;
    output = bounded((pid.proportional + pid.integral) - pid.derivative,
                      pid.output_minimum, pid.output_maximum);
    pid.previous_input = input;
}

bool heater_configure_boiler(HeaterController& heater, BoilerType boiler) noexcept {
    switch (boiler) {
        case BoilerType::Big: return pid_set_tunings(heater.pid, 34.0, 13.0, 3.0);
        case BoilerType::Small: return pid_set_tunings(heater.pid, 30.0, 10.0, 0.0);
        default: return false;
    }
}

bool heater_initialize_outputs(HeaterController& heater, const HeaterIo& io) noexcept {
    if (!io.configure_output || !io.write_pin) return false;
    io.configure_output(io.context, heater.output_pin);
    io.write_pin(io.context, heater.output_pin, false);
    io.configure_output(io.context, OriginalHeaterEnablePin);
    return heater_enable(heater, io, false);
}

bool heater_enable(HeaterController& heater, const HeaterIo& io, bool enabled) noexcept {
    if (!io.write_pin) return false;
    heater.state.enabled = enabled;
    io.write_pin(io.context, OriginalHeaterEnablePin, enabled);
    return true;
}

void heater_start(HeaterController& heater, double wort_celsius) noexcept {
    heater.state.started = true;
    pid_set_mode(heater.pid, true, wort_celsius, heater.state.output_ticks);
}

bool heater_stop(HeaterController& heater, const HeaterIo& io, double wort_celsius) noexcept {
    if (!io.write_pin) return false;
    heater.state.output_ticks = 0.0;
    heater.state.started = false;
    io.write_pin(io.context, heater.output_pin, false);
    pid_set_mode(heater.pid, false, wort_celsius, heater.state.output_ticks);
    return true;
}

bool heater_set_target(HeaterController& heater, double requested, double boil) noexcept {
    if (!std::isfinite(requested) || !std::isfinite(boil)) return false;
    const double maximum = boil - 2.0;
    if (requested <= maximum) {
        heater.state.target_excess = 0.0;
        heater.state.target_celsius = requested;
    } else {
        heater.state.target_excess = requested - maximum;
        heater.state.target_celsius = maximum;
    }
    return true;
}

bool heater_tick_20ms(HeaterController& heater, const HeaterIo& io) noexcept {
    if (!io.write_pin) return false;
    if (heater.state.started) {
        if (heater.countdown > 0) --heater.countdown;
        else if (heater.countdown == 0) io.write_pin(io.context, heater.output_pin, false);
    }
    return true;
}

bool heater_tick_1s(HeaterController& heater, HeaterSensors& sensors,
                    ThermalSafety& safety, const ThermalLimits& limits,
                    std::uint32_t now_ms, const HeaterIo& io) noexcept {
    if (!io.write_pin) return false;
    heater.second_phase = !heater.second_phase;
    if (!heater.second_phase) return true;
    if (safety.enabled) refresh_safety(sensors, safety, limits, now_ms);
    const double wort = sensors.temperatures[static_cast<std::size_t>(TempSensorId::Wort)].celsius;
    const double hex = sensors.temperatures[static_cast<std::size_t>(TempSensorId::Hex)].celsius;
    sensors.wort_rate = ((wort - sensors.previous_wort) * 0.02) * 30.0 + sensors.wort_rate * 0.98;
    sensors.hex_rate = ((hex - sensors.previous_hex) * 0.02) * 30.0 + sensors.hex_rate * 0.98;
    sensors.previous_wort = wort; sensors.previous_hex = hex;
    if (!heater.state.started) return true;
    bool high = false;
    if (safety.too_hot == 0 && safety.too_old == 0) {
        pid_compute(heater.pid, wort, heater.state.target_celsius, heater.state.output_ticks);
        heater.state.integral_display = heater.pid.integral;
        heater.countdown = static_cast<std::int32_t>(heater.state.output_ticks);
        high = heater.state.output_ticks > 0.0;
    } else {
        heater.state.output_ticks = 0.0;
        heater.state.integral_display = 0.0;
    }
    io.write_pin(io.context, heater.output_pin, high);
    return true;
}

}} // namespace zseries::oem
