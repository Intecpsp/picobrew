// Reconstructed from PicoStill 0.0.35. Original entry addresses are evidence,
// never runtime dependencies. This is not the lost vendor source.
#pragma once
#include "picostill/enums.hpp"
#include <array>
#include <cstdint>

namespace picostill { namespace oem {

// Both OEM assertion forms return. The counted form logs, increments a
// uint32_t counter and delays 200 ms; the simple form only logs.
struct DiagnosticPorts {
    virtual ~DiagnosticPorts() = default;
    virtual void assertion(const char* file, unsigned line, const char* text) = 0;
    virtual void message(const char* text) = 0;
    virtual void delay_ms(std::uint32_t duration) = 0;
};
class Diagnostics {
public:
    explicit Diagnostics(DiagnosticPorts& ports) noexcept : ports_(ports) {}
    std::uint32_t assertion_count = 0;
    void require(bool condition, const char* file, unsigned line, const char* text);
    void check(bool condition, const char* text);
    void message(const char* text) { ports_.message(text); }
private:
    DiagnosticPorts& ports_;
};

// Original 0x400d46b8: the assertion does not short-circuit N <= 0.
double exponential_moving_average(double previous, std::int32_t count,
                                  double sample, Diagnostics& diagnostics);

struct ThermistorParameters {
    double a, b, c, series_resistance_ohms;
    float adc_max;
};
// 0x400d4074, parameters supplied by the caller. Board calibration has not
// been inferred from another model. Zero ADC logs and continues as in OEM.
double thermistor_kelvin(const ThermistorParameters& parameters, std::int32_t adc,
                        Diagnostics& diagnostics);

struct SensorMetrics {
    std::uint32_t temperature_samples = 0;
    std::uint32_t pressure_samples = 0;
    std::array<float, 5> success_rates = {{0, 0, 0, 0, 0}};
    std::array<float, 5> averages = {{0, 0, 0, 0, 0}};
    bool sampling_threshold_met() const noexcept;
    bool read_success_rates_good(Diagnostics& diagnostics) const;
    bool values_in_range(Diagnostics& diagnostics) const;
};
struct FanMetrics {
    std::uint32_t samples = 0;
    float rpm_average = 0;
    bool running_fast_enough(Diagnostics& diagnostics) const;
};
struct FanStatus {
    bool enabled = false;
    std::uint8_t speed = 0;
    FanMetrics metrics;
};
bool fans_have_issue(const std::array<FanStatus, 2>& fans, Diagnostics& diagnostics);
bool safe_to_resume_pump(float output_temperature, float pressure,
                         float maximum_temperature = 35.0f,
                         float recovery_hysteresis = 2.8f,
                         float minimum_pressure = 10.0f) noexcept;
Animation error_animation(StillError error, Diagnostics& diagnostics);
void latch_first_error(StillError& current, StillError error, Diagnostics& diagnostics);

enum class ButtonState : std::uint32_t { Idle = 0, Pressed = 1, EventPending = 2 };
struct ButtonPorts {
    virtual ~ButtonPorts() = default;
    virtual std::uint32_t millis() = 0;
    virtual bool gpio_high() = 0; // Original active-low GPIO 13.
    virtual void button_event(bool is_long) = 0;
};
class Button {
public:
    Button(ButtonPorts& ports, Diagnostics& diagnostics) noexcept
        : ports_(ports), diagnostics_(diagnostics) {}
    bool pressed = false;
    std::uint32_t press_count = 0;
    ButtonState state = ButtonState::Idle;
    std::uint32_t changed_at = 0;
    void set_state(ButtonState next);
    void on_edge(bool is_pressed);
    void poll();
private:
    ButtonPorts& ports_;
    Diagnostics& diagnostics_;
    void emit(bool is_long);
};

}} // namespace picostill::oem
