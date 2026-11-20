// PicoStill 0.0.35 actuator, indicator and pump-test reconstruction.
#pragma once
#include "picostill/core.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace picostill { namespace oem {
struct HardwarePorts {
    virtual ~HardwarePorts() = default;
    virtual std::uint32_t millis() = 0;
    virtual void pin_mode(std::uint8_t pin, unsigned mode) = 0;
    virtual bool digital_read(std::uint8_t pin) = 0;
    virtual void digital_write(std::uint8_t pin, bool high) = 0;
    virtual void pwm_setup(std::uint8_t channel, double frequency, unsigned resolution) = 0;
    virtual void pwm_attach(std::uint8_t pin, std::uint8_t channel) = 0;
    virtual void pwm_write(std::uint8_t channel, std::uint8_t duty) = 0;
    virtual void variable(const char* scope, const char* name, const char* value) = 0;
};

class OutputPin {
public:
    OutputPin(HardwarePorts& ports, std::uint8_t gpio) noexcept : pin(gpio), ports_(ports) {}
    std::uint8_t pin;
    bool enabled = false;
    void set(bool value);
    void set_pump(bool value);
private:
    HardwarePorts& ports_;
};
class Fan {
public:
    Fan(HardwarePorts& ports, Diagnostics& diagnostics, std::uint8_t pwm_channel,
        std::uint8_t output, std::uint8_t tach) noexcept
        : channel(pwm_channel), output_pin(output), tach_pin(tach), ports_(ports), diagnostics_(diagnostics) {}
    std::uint8_t channel, output_pin, tach_pin;
    std::uint64_t next_sample = 0;
    bool tach_high = false, enabled = false;
    std::uint8_t speed = 0;
    FanMetrics metrics;
    std::uint32_t pulses = 0;
    void setup();
    void set(bool value, std::uint8_t duty);
    void sample();
private:
    HardwarePorts& ports_;
    Diagnostics& diagnostics_;
};
class FanBank {
public:
    // Pins/channels are from the original PicoStill constructor 0x400d82dc.
    FanBank(HardwarePorts& ports, Diagnostics& diagnostics) noexcept
        : power(ports,18), first(ports,diagnostics,1,12,5), second(ports,diagnostics,2,2,17), ports_(ports) {}
    OutputPin power;
    Fan first, second;
    bool second_enabled() const noexcept { return power.enabled && second.enabled; }
    void set_power(bool enabled);
    void set_first(bool enabled);
private:
    HardwarePorts& ports_;
};

struct AnimationProgram { const char* text; std::int32_t repeat; };
// Bounds-safe source access; OEM 0x400d5dbc itself assumes a valid enum index.
const AnimationProgram* animation_program(Animation animation) noexcept;
class Leds {
public:
    explicit Leds(HardwarePorts& ports, Diagnostics& diagnostics) noexcept
        : ports_(ports), diagnostics_(diagnostics) {}
    std::array<std::uint8_t,3> pins = {{26,25,33}};
    std::array<bool,3> values = {{false,false,false}};
    std::array<char,64> program = {{0}};
    std::uint32_t length = 0, remaining = 63;
    std::int32_t repeat = 0;
    std::uint32_t completed = 0, position = 0, deadline = 0;
    void setup();
    void set_all(bool high);
    void toggle(unsigned color);
    void start(const char* text, std::int32_t count);
    bool start(Animation animation);
    void poll();
private:
    HardwarePorts& ports_;
    Diagnostics& diagnostics_;
    void clear_program() noexcept;
};

struct HardwareSnapshot {
    std::uint64_t captured_at = 0;
    std::uint32_t sensor_samples = 0, fan1_samples = 0, fan2_samples = 0;
    std::array<float,5> values = {{0,0,0,0,0}};
    std::array<float,5> averages = {{0,0,0,0,0}};
    std::array<float,5> success_rates = {{0,0,0,0,0}};
    std::uint32_t fan1_speed = 0;
    float fan1_rpm = 0;
    std::uint32_t fan2_speed = 0;
    float fan2_rpm = 0;
};
static_assert(sizeof(HardwareSnapshot)==96, "Recovered snapshot size");
struct SnapshotPorts {
    virtual ~SnapshotPorts() = default;
    virtual HardwareSnapshot capture() = 0;
    virtual void capture_into(HardwareSnapshot& target) { target=capture(); }
};
class PumpTest {
public:
    PumpTest(HardwarePorts& hardware, Diagnostics& diagnostics, SnapshotPorts& snapshots) noexcept
        : hardware_(hardware), diagnostics_(diagnostics), snapshots_(snapshots) {}
    PumpTestState state = PumpTestState::Starting;
    std::uint64_t started_at = 0, vacuum_duration = 0, deadline_runtime = 0;
    HardwareSnapshot current, result;
    float initial_pressure = 0, clog_pressure = 0, leak_pressure = 0;
    StillError error = StillError::None;
    void set_state(PumpTestState next);
    void tick(std::uint64_t now, std::uint64_t runtime);
private:
    HardwarePorts& hardware_;
    Diagnostics& diagnostics_;
    SnapshotPorts& snapshots_;
};
}} // namespace picostill::oem
