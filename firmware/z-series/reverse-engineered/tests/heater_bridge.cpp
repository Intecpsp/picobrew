// Test adapter only: flat records allow independent original-code fixtures to
// exercise the typed reconstruction without depending on a native ABI layout.
#include "zseries/heater.hpp"
#include <cstdint>
#include <cstring>

namespace {
using namespace zseries::oem;

struct Events {
    std::uint32_t* words;
    std::uint32_t count;
    void append(std::uint32_t kind, std::uint8_t pin, std::uint32_t value) {
        words[count * 3] = kind;
        words[count * 3 + 1] = pin;
        words[count * 3 + 2] = value;
        ++count;
    }
};

void configure(void* context, std::uint8_t pin) {
    static_cast<Events*>(context)->append(0, pin, 2);
}
void write(void* context, std::uint8_t pin, bool value) {
    static_cast<Events*>(context)->append(1, pin, value);
}

void load(const double* values, const std::uint32_t* words,
          HeaterController& h, HeaterSensors& s, ThermalSafety& t) {
    h.pid.display_kp = values[0]; h.pid.display_ki = values[1]; h.pid.display_kd = values[2];
    h.pid.kp = values[3]; h.pid.ki = values[4]; h.pid.kd = values[5];
    h.pid.output_minimum = values[6]; h.pid.output_maximum = values[7];
    h.pid.error = values[8]; h.pid.proportional = values[9]; h.pid.integral = values[10];
    h.pid.derivative = values[11]; h.pid.previous_input = values[12];
    h.state.target_celsius = values[13]; h.state.target_excess = values[14];
    h.state.output_ticks = values[15]; h.state.integral_display = values[16];
    s.previous_wort = values[17]; s.previous_hex = values[18];
    s.wort_rate = values[19]; s.hex_rate = values[20]; s.estimated_boil_celsius = values[21];
    for (unsigned i = 0; i < 4; ++i) {
        s.temperatures[i].celsius = values[22 + i];
        s.temperatures[i].updated_ms = words[11 + i];
    }
    h.pid.automatic = words[0] != 0; h.pid.sample_time_ms = words[1]; h.pid.reverse = words[2] != 0;
    h.state.enabled = words[3] != 0; h.state.started = words[4] != 0;
    std::memcpy(&h.countdown, words + 5, sizeof(h.countdown));
    h.second_phase = words[6] != 0; t.enabled = words[7] != 0;
    t.too_hot = static_cast<std::uint8_t>(words[8]);
    t.too_cold = static_cast<std::uint8_t>(words[9]);
    t.too_old = static_cast<std::uint8_t>(words[10]);
    h.output_pin = static_cast<std::uint8_t>(words[16]);
}

void save(double* values, std::uint32_t* words,
          const HeaterController& h, const HeaterSensors& s, const ThermalSafety& t) {
    values[0] = h.pid.display_kp; values[1] = h.pid.display_ki; values[2] = h.pid.display_kd;
    values[3] = h.pid.kp; values[4] = h.pid.ki; values[5] = h.pid.kd;
    values[6] = h.pid.output_minimum; values[7] = h.pid.output_maximum;
    values[8] = h.pid.error; values[9] = h.pid.proportional; values[10] = h.pid.integral;
    values[11] = h.pid.derivative; values[12] = h.pid.previous_input;
    values[13] = h.state.target_celsius; values[14] = h.state.target_excess;
    values[15] = h.state.output_ticks; values[16] = h.state.integral_display;
    values[17] = s.previous_wort; values[18] = s.previous_hex;
    values[19] = s.wort_rate; values[20] = s.hex_rate; values[21] = s.estimated_boil_celsius;
    for (unsigned i = 0; i < 4; ++i) {
        values[22 + i] = s.temperatures[i].celsius;
        words[11 + i] = s.temperatures[i].updated_ms;
    }
    words[0] = h.pid.automatic; words[1] = h.pid.sample_time_ms; words[2] = h.pid.reverse;
    words[3] = h.state.enabled; words[4] = h.state.started;
    std::memcpy(words + 5, &h.countdown, sizeof(h.countdown));
    words[6] = h.second_phase; words[7] = t.enabled;
    words[8] = t.too_hot; words[9] = t.too_cold; words[10] = t.too_old;
    words[16] = h.output_pin;
}
} // namespace

// action IDs are listed by tools/validate_heater.py. At most four GPIO events
// are produced by one operation; the caller supplies room for sixteen.
extern "C" int heater_action(unsigned action, double* values, std::uint32_t* words,
                              const double* args, std::uint32_t* gpio,
                              std::uint32_t* gpio_count, int missing_io) {
    HeaterController h;
    HeaterSensors s;
    ThermalSafety t;
    load(values, words, h, s, t);
    Events events = {gpio, 0};
    HeaterIo io = {&events, missing_io ? nullptr : configure, missing_io ? nullptr : write};
    bool result = true;
    switch (action) {
        case 0: result = pid_set_tunings(h.pid, args[0], args[1], args[2]); break;
        case 1: pid_initialize(h.pid, s.temperatures[1].celsius); break;
        case 2: pid_set_mode(h.pid, args[0] != 0, s.temperatures[1].celsius, h.state.output_ticks); break;
        case 3: result = pid_set_output_limits(h.pid, args[0], args[1], h.state.output_ticks); break;
        case 4: pid_set_direction(h.pid, args[0] != 0); break;
        case 5: pid_compute(h.pid, s.temperatures[1].celsius, h.state.target_celsius, h.state.output_ticks); break;
        case 6: result = heater_configure_boiler(h, static_cast<BoilerType>(static_cast<std::uint32_t>(args[0]))); break;
        case 7: result = heater_initialize_outputs(h, io); break;
        case 8: result = heater_enable(h, io, args[0] != 0); break;
        case 9: heater_start(h, s.temperatures[1].celsius); break;
        case 10: result = heater_stop(h, io, s.temperatures[1].celsius); break;
        case 11: result = heater_set_target(h, args[0], s.estimated_boil_celsius); break;
        case 12: result = heater_tick_20ms(h, io); break;
        case 13: result = heater_tick_1s(h, s, t, original_thermal_limits(), words[15], io); break;
        default: return -1;
    }
    save(values, words, h, s, t);
    *gpio_count = events.count;
    return result;
}
