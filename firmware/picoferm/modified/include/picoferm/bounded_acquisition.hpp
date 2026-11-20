// Modified foreground acquisition. The OEM register sequence and conversion
// are preserved, while every readiness wait has a bounded deadline.
#pragma once
#include <cstdint>

namespace picoferm { namespace standalone {
struct SensorIo {
    void* context = nullptr;
    bool (*begin)(void*, unsigned sda, unsigned scl, unsigned address) = nullptr;
    bool (*read)(void*, std::uint8_t reg, std::uint8_t& value) = nullptr;
    bool (*write)(void*, std::uint8_t reg, std::uint8_t value) = nullptr;
    bool (*read24)(void*, std::uint8_t reg, std::uint32_t& value) = nullptr;
    bool (*battery)(void*, float& volts) = nullptr;
};
enum class SensorState : std::uint8_t { Uninitialized, Initializing, Idle, Sampling, Fault };
enum class SensorFault : std::uint8_t { None, MissingPort, Bus, PowerTimeout, CalibrationTimeout, ReadyTimeout, InvalidPressure, InvalidTemperature, InvalidBattery };
struct SensorSample {
    float temperature_f = 0, battery_v = 0;
    std::int32_t pressure_raw = 0, temperature_raw = 0;
    std::uint32_t measured_at = 0, sequence = 0;
    bool valid = false;
};
class BoundedAcquisition {
public:
    explicit BoundedAcquisition(SensorIo io) noexcept : io_(io) {}
    void begin(std::uint32_t now);
    // Initialization may be queued; fault recovery requires an explicit begin.
    bool request(std::uint32_t now);
    void poll(std::uint32_t now);
    SensorState state() const noexcept { return state_; }
    SensorFault fault() const noexcept { return fault_; }
    const SensorSample& sample() const noexcept { return sample_; }
    bool busy() const noexcept { return state_ == SensorState::Initializing || state_ == SensorState::Sampling; }
    static const char* fault_name(SensorFault) noexcept;
private:
    SensorIo io_;
    SensorState state_ = SensorState::Uninitialized;
    SensorFault fault_ = SensorFault::None;
    SensorSample sample_, staged_;
    std::uint32_t deadline_ = 0, wait_deadline_ = 0;
    unsigned initialize_step_ = 0, sample_step_ = 0;
    bool requested_ = false, waiting_ = false;
    bool update(std::uint8_t reg, unsigned bit, bool set);
    void fail(SensorFault);
    void initialize(std::uint32_t now);
    void acquire(std::uint32_t now);
};
}}
