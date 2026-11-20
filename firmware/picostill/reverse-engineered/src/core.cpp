// Editable OEM reconstruction. See analysis/source-evidence.json for entries
// and tests/fixtures/core-original.json for original instruction execution.
#include "picostill/core.hpp"
#include <cmath>

namespace picostill { namespace oem {

void Diagnostics::require(bool condition, const char* file, unsigned line, const char* text) {
    if (condition) return;
    ports_.assertion(file, line, text); // 0x400d4678, diagnostic formatting boundary.
    ++assertion_count;
    ports_.delay_ms(200);
}
void Diagnostics::check(bool condition, const char* text) {
    if (!condition) {
        ports_.message("ERR ASSERT "); // 0x400d405c.
        ports_.message(text);
    }
}
double exponential_moving_average(double previous, std::int32_t count,
                                  double sample, Diagnostics& diagnostics) {
    diagnostics.require(count > 0, "MathHelpers.h", 0x38, "numSamples > 0");
    if (count == 1) return sample;
    const auto next = static_cast<std::int32_t>(static_cast<std::uint32_t>(count) + 1u);
    const double weight = 2.0 / static_cast<double>(next);
    return (1.0 - weight) * previous + weight * sample;
}
double thermistor_kelvin(const ThermistorParameters& p, std::int32_t adc,
                        Diagnostics& diagnostics) {
    diagnostics.check(adc != 0, "adc != 0");
    const double resistance = p.series_resistance_ohms /
        (static_cast<double>(p.adc_max) / static_cast<double>(adc) - 1.0);
    const double logarithm = std::log(resistance);
    const double linear = p.a + logarithm * p.b;
    const double cubic = ((logarithm * p.c) * logarithm) * logarithm;
    return 1.0 / (linear + cubic);
}
bool SensorMetrics::sampling_threshold_met() const noexcept {
    return temperature_samples > 1 || pressure_samples > 1; // 0x4014a084.
}
bool SensorMetrics::read_success_rates_good(Diagnostics& diagnostics) const {
    diagnostics.check(sampling_threshold_met(), "isSamplingThresholdMet()");
    diagnostics.check(sampling_threshold_met(), "SENSOR METRIC isSamplingThresholdMet()");
    for (float rate : success_rates) if (!(rate > 0.75f)) return false;
    return true; // 0x400d4204.
}
bool SensorMetrics::values_in_range(Diagnostics& diagnostics) const {
    // OEM postpones range rejection until the sampling/read-quality gate.
    if (!sampling_threshold_met() || !read_success_rates_good(diagnostics)) return true;
    for (unsigned i = 0; i < 4; ++i)
        if (!(averages[i] >= 0.0f && averages[i] <= 80.0f)) return false;
    return averages[4] >= 0.0f && averages[4] <= 16.0f; // 0x400d426c.
}
bool FanMetrics::running_fast_enough(Diagnostics& diagnostics) const {
    diagnostics.check(samples >= 5, "FAN METRIC isSamplingThresholdMet()");
    return rpm_average > 1600.0f; // 0x400d4154.
}
bool fans_have_issue(const std::array<FanStatus, 2>& fans, Diagnostics& diagnostics) {
    for (const auto& fan : fans)
        if (fan.enabled && fan.speed >= 155 && fan.metrics.samples >= 5 &&
            !fan.metrics.running_fast_enough(diagnostics)) return true;
    return false; // 0x400d4180.
}
bool safe_to_resume_pump(float output_temperature, float pressure,
                         float maximum_temperature, float recovery_hysteresis,
                         float minimum_pressure) noexcept {
    return output_temperature <= maximum_temperature - recovery_hysteresis &&
           pressure > minimum_pressure; // 0x4014a0a0; strict pressure comparison.
}
Animation error_animation(StillError error, Diagnostics& diagnostics) {
    const auto number = static_cast<std::uint32_t>(error);
    diagnostics.require(number != 0, "PicoStill.h", 0x102, "error != StillError::None");
    if (number >= 1 && number <= 20) return static_cast<Animation>(number + 18);
    diagnostics.message("Unmapped StillError");
    return Animation::SlowRed; // 0x400d554c.
}
void latch_first_error(StillError& current, StillError error, Diagnostics& diagnostics) {
    diagnostics.require(current == StillError::None, "PicoStill.h", 0x129,
                        "_error == StillError::None");
    if (current == StillError::None) current = error; // 0x400d5734.
}
void Button::set_state(ButtonState next) {
    diagnostics_.check(state != next, "_state != state");
    if (state == next) return;
    state = next;
    changed_at = ports_.millis();
    if (state == ButtonState::Idle) press_count = 0;
    else if (state == ButtonState::Pressed) ++press_count; // 0x400d43d0.
}
void Button::on_edge(bool is_pressed) {
    switch (state) {
    case ButtonState::Pressed:
        diagnostics_.check(!is_pressed, "!isPressed Pressed");
        if (!is_pressed) set_state(ButtonState::EventPending);
        break;
    case ButtonState::EventPending:
        diagnostics_.check(is_pressed, "isPressed EventPending");
        if (is_pressed) set_state(ButtonState::Pressed);
        break;
    case ButtonState::Idle:
        if (is_pressed) set_state(ButtonState::Pressed);
        break;
    default: break;
    }
}
void Button::emit(bool is_long) {
    ports_.button_event(is_long); // Callback observes press_count before reset.
    set_state(ButtonState::Idle); // 0x400d4408.
}
void Button::poll() {
    const auto now = ports_.millis();
    const bool current = !ports_.gpio_high();
    if (current != pressed) {
        pressed = current;
        on_edge(current);
    }
    // Preserve the OEM absolute unsigned comparison, including wrap behavior;
    // a corrected wrap-safe policy belongs in modified firmware.
    if (state == ButtonState::Pressed && now > changed_at + std::uint32_t{5000}) emit(true);
    else if (state == ButtonState::EventPending && now > changed_at + std::uint32_t{200}) emit(false);
}

}} // namespace picostill::oem
