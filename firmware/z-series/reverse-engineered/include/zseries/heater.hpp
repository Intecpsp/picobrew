// Recovered Z119 heater/PID/timer state, with explicit GPIO boundaries.
#pragma once
#include "zseries/temperature.hpp"
#include <array>
#include <cstdint>

namespace zseries { namespace oem {

constexpr std::uint8_t OriginalHeaterOutputPin = 12;
constexpr std::uint8_t OriginalHeaterEnablePin = 19;

struct TemperatureSample {
    double celsius = 0.0;
    std::uint32_t updated_ms = 0;
};

struct HeaterSensors {
    std::array<TemperatureSample, TempSensorIdCount> temperatures;
    double previous_wort = 0.0;
    double previous_hex = 0.0;
    double wort_rate = 0.0;
    double hex_rate = 0.0;
    double estimated_boil_celsius = 100.0;
};

struct ThermalSafety {
    bool enabled = false;
    std::uint8_t too_hot = 0;
    std::uint8_t too_cold = 0;
    std::uint8_t too_old = 0;
};

struct ThermalLimit {
    double minimum_celsius;
    double maximum_celsius;
    std::uint32_t maximum_age_ms;
};
using ThermalLimits = std::array<ThermalLimit, TempSensorIdCount>;
const ThermalLimits& original_thermal_limits() noexcept;

struct PidState {
    double display_kp = 0.0, display_ki = 0.0, display_kd = 0.0;
    double kp = 0.0, ki = 0.0, kd = 0.0;
    bool automatic = false;
    std::uint32_t sample_time_ms = 100;
    double output_minimum = 0.0, output_maximum = 100.0;
    bool reverse = false;
    double error = 0.0, proportional = 0.0, integral = 0.0, derivative = 0.0;
    double previous_input = 0.0;
};

// PID computation accepts finite sensor/state values and ordered output limits.
// The timer's original double-to-countdown conversion additionally requires
// an output representable by int32_t. Original setup uses the range 0..100.

struct HeaterState {
    double target_celsius = 0.0;
    double target_excess = 0.0;
    bool enabled = false;
    bool started = false;
    double output_ticks = 0.0;
    double integral_display = 0.0;
};

struct HeaterController {
    HeaterState state;
    PidState pid;
    std::uint8_t output_pin = OriginalHeaterOutputPin;
    std::int32_t countdown = 0;
    bool second_phase = false;
};

// Required GPIO callbacks for operations that perform I/O. A missing callback
// is rejected by a false result before an operation mutates state. These
// callbacks do not claim a physical ESP32 installation or GPIO configuration.
struct HeaterIo {
    void* context;
    void (*configure_output)(void*, std::uint8_t);
    void (*write_pin)(void*, std::uint8_t, bool);
};

bool pid_set_tunings(PidState& pid, double kp, double ki, double kd) noexcept;
void pid_initialize(PidState& pid, double input) noexcept;
void pid_set_mode(PidState& pid, bool automatic, double input, double& output) noexcept;
bool pid_set_output_limits(PidState& pid, double minimum, double maximum, double& output) noexcept;
void pid_set_direction(PidState& pid, bool reverse) noexcept;
void pid_compute(PidState& pid, double input, double target, double& output) noexcept;

// Valid-domain boiler gains at 0x400de27c, followed by original setup tunings.
bool heater_configure_boiler(HeaterController& heater, BoilerType boiler) noexcept;
bool heater_initialize_outputs(HeaterController& heater, const HeaterIo& io) noexcept;
bool heater_enable(HeaterController& heater, const HeaterIo& io, bool enabled) noexcept;
void heater_start(HeaterController& heater, double wort_celsius) noexcept;
bool heater_stop(HeaterController& heater, const HeaterIo& io, double wort_celsius) noexcept;
bool heater_set_target(HeaterController& heater, double requested_celsius,
                       double estimated_boil_celsius) noexcept;

// Original 0x40080540: a positive counter is decremented; a zero counter
// writes LOW. Thus reaching zero by decrement does not switch LOW until
// the next callback. Negative values do nothing. This detail is preserved.
bool heater_tick_20ms(HeaterController& heater, const HeaterIo& io) noexcept;

// Original 0x400805dc. Toggle each callback, performing work every other call.
// Safety flags refresh only when enabled. Heater output is gated by too_hot
// and too_old; the original does not gate it on too_cold or enabled here.
bool heater_tick_1s(HeaterController& heater, HeaterSensors& sensors,
                    ThermalSafety& safety, const ThermalLimits& limits,
                    std::uint32_t now_ms, const HeaterIo& io) noexcept;

}} // namespace zseries::oem
