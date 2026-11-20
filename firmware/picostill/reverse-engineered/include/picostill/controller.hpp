// PicoStill 0.0.35 hardware lifecycle, diagnostics and program controller.
#pragma once
#include "picostill/hardware.hpp"
#include "picostill/sensors.hpp"

namespace picostill { namespace oem {
struct FullTestResult {
    StillError error = StillError::None;
    std::uint32_t reserved = 0;
    std::uint64_t runtime = 0;
    std::array<float,5> success_rates = {{0,0,0,0,0}};
    std::array<float,5> values = {{0,0,0,0,0}};
};
// Original observers/report serializers are explicit application boundaries.
struct ControllerPorts {
    virtual ~ControllerPorts() = default;
    virtual void sensors_sampled() = 0;
    virtual void snapshot_report(const HardwareSnapshot& snapshot) = 0;
    virtual void full_test_report(const FullTestResult& result) = 0;
    virtual void controller_changed() = 0;
    virtual void console_message(const char* message) = 0;
};

class Hardware final : public SnapshotPorts {
public:
    Hardware(HardwarePorts& ports, I2cPorts& i2c, Diagnostics& diagnostics, ControllerPorts& events) noexcept
        : fans(ports,diagnostics),pump(ports,19),sensors(i2c,diagnostics),ports_(ports),diagnostics_(diagnostics),events_(events) {}
    FanBank fans;
    OutputPin pump;
    Sensors sensors;
    bool sensors_enabled = false, record_pressure = false;
    std::uint32_t next_sample = 0, sample_interval = 2000, recorded_count = 0;
    float failed_pressure = -1.0f, maximum_temperature = 35.0f, hysteresis = 2.8f, minimum_pressure = 10.0f;
    std::array<float,200> recorded_pressure = {{0}};
    StillError error = StillError::None;
    HardwareSnapshot error_snapshot;
    bool enabled() const noexcept;
    bool sensors_ready() const noexcept { return sensors.metrics.sampling_threshold_met(); }
    bool fans_ready() const noexcept { return fans.first.metrics.samples>4 && fans.second.metrics.samples>4; }
    bool sensors_in_range();
    bool fans_issue();
    bool safe_to_resume() const noexcept;
    void enable_sensors(bool value);
    void enable_sensors_internal(bool value);
    void enable_pressure_recording();
    void poll();
    void check_error();
    HardwareSnapshot capture() override;
    void capture_into(HardwareSnapshot& target) override;
    std::uint32_t millis() { return ports_.millis(); }
private:
    HardwarePorts& ports_;
    Diagnostics& diagnostics_;
    ControllerPorts& events_;
};

bool sensor_connections_match(SnapshotPorts& snapshots);
class ConnectionTest {
public:
    ConnectionTest(HardwarePorts& ports, Diagnostics& diagnostics) noexcept : ports_(ports),diagnostics_(diagnostics) {}
    std::uint64_t started_at = 0;
    bool started = false, done = false;
    StillError error = StillError::None;
    HardwareSnapshot result;
    void reset() noexcept;
    void finish(Hardware& hardware);
    void tick(std::uint64_t runtime,Hardware& hardware);
private:
    HardwarePorts& ports_;
    Diagnostics& diagnostics_;
};
class PinTest {
public:
    PinTest(HardwarePorts& ports,ControllerPorts& events) noexcept : ports_(ports),events_(events) {}
    std::uint64_t started_at = 0;
    bool started = false, done = false, sensors_passed = false;
    HardwareSnapshot result;
    StillError error = StillError::None;
    // OEM resets started/done but leaves sensors_passed untouched.
    void reset() noexcept;
    void tick(std::uint64_t runtime,SnapshotPorts& snapshots);
private:
    HardwarePorts& ports_;
    ControllerPorts& events_;
};

class Controller {
public:
    Controller(Hardware& hardware,HardwarePorts& ports,Diagnostics& diagnostics,ControllerPorts& events) noexcept
        : pump_test(ports,diagnostics,hardware),connection_test(ports,diagnostics),pin_test(ports,events),
          hardware_(hardware),ports_(ports),diagnostics_(diagnostics),events_(events) {}
    HWState state = HWState::Off;
    std::uint64_t state_changed_at = 0;
    ProgramId program = ProgramId::None;
    std::uint32_t program_started_at = 0, overtemperature_deadline = 0;
    std::uint32_t overtemperature_holdoff = 120000, cleaning_duration = 20000;
    StillError error = StillError::None;
    FullTestResult full_result;
    PumpTest pump_test;
    ConnectionTest connection_test;
    PinTest pin_test;
    bool running() const noexcept { return state != HWState::Off; }
    void set_state(HWState next);
    void start(ProgramId requested);
    void stop();
    void poll();
private:
    Hardware& hardware_;
    HardwarePorts& ports_;
    Diagnostics& diagnostics_;
    ControllerPorts& events_;
    void reset_pump_test() noexcept;
};
}} // namespace picostill::oem
