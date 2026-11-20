#include "picoferm/bounded_acquisition.hpp"
#include "picoferm/hardware.hpp"
#include <cmath>
#include <cstring>

namespace picoferm { namespace standalone {
namespace {
bool due(std::uint32_t now, std::uint32_t deadline) { return (now - deadline) < 0x80000000u; }
struct RegisterStep { std::uint8_t reg, bit; bool set; };
// Waits are inserted after step 2 (power ready) and step 8 (calibration).
const RegisterStep initialization[] = {
    {0,0,true}, {0,0,false}, {0,1,true},
    {0,2,true}, {0x15,4,true}, {0x15,5,true}, {0x1b,0,true}, {0x1b,4,false}, {2,2,true},
    {0x11,5,true}, {0,4,true},
    {2,4,false}, {2,5,true}, {2,6,false}, // OEM sample rate
    {0,7,true}, {1,5,true}, {1,4,false}, {1,3,true}, // 3.0 V reference
    {1,2,false}, {1,1,false}, {1,0,false} // unity gain
};
}
const char* BoundedAcquisition::fault_name(SensorFault fault) noexcept {
    switch (fault) {
    case SensorFault::None:return "none";
    case SensorFault::MissingPort:return "sensor_port";
    case SensorFault::Bus:return "sensor_bus";
    case SensorFault::PowerTimeout:return "adc_power_timeout";
    case SensorFault::CalibrationTimeout:return "adc_calibration_timeout";
    case SensorFault::ReadyTimeout:return "adc_ready_timeout";
    case SensorFault::InvalidPressure:return "invalid_pressure_sample";
    case SensorFault::InvalidTemperature:return "invalid_temperature_sample";
    case SensorFault::InvalidBattery:return "invalid_battery_sample";
    }
    return "sensor_fault";
}
void BoundedAcquisition::fail(SensorFault fault) {
    fault_ = fault; state_ = SensorState::Fault; sample_.valid = false;
    requested_ = waiting_ = false;
}
bool BoundedAcquisition::update(std::uint8_t reg, unsigned bit, bool set) {
    std::uint8_t value = 0;
    if (!io_.read(io_.context, reg, value)) return false;
    const auto mask = static_cast<std::uint8_t>(1u << bit);
    return io_.write(io_.context, reg, set ? value | mask : value & ~mask);
}
void BoundedAcquisition::begin(std::uint32_t now) {
    sample_.valid = false; requested_ = waiting_ = false;
    initialize_step_ = sample_step_ = 0; deadline_ = now; fault_ = SensorFault::None;
    if (!io_.begin || !io_.read || !io_.write || !io_.read24 || !io_.battery) { fail(SensorFault::MissingPort); return; }
    if (!io_.begin(io_.context, 2, 14, 42)) { fail(SensorFault::Bus); return; }
    state_ = SensorState::Initializing;
}
bool BoundedAcquisition::request(std::uint32_t now) {
    if (state_ == SensorState::Initializing && !requested_) { requested_ = true; return true; }
    if (state_ != SensorState::Idle) return false;
    staged_ = SensorSample(); sample_step_ = 1; deadline_ = now; waiting_ = false;
    state_ = SensorState::Sampling; return true;
}
void BoundedAcquisition::initialize(std::uint32_t now) {
    if (waiting_) {
        std::uint8_t value = 0;
        const bool power = initialize_step_ == 3;
        if (!io_.read(io_.context, power ? 0 : 2, value)) { fail(SensorFault::Bus); return; }
        const bool complete = power ? (value & 8u) != 0 : (value & 4u) == 0;
        if (!complete) {
            if (due(now, wait_deadline_)) fail(power ? SensorFault::PowerTimeout : SensorFault::CalibrationTimeout);
            else deadline_ = now + 5;
            return;
        }
        waiting_ = false; // The next register operation occurs on a later poll.
        return;
    }
    if (initialize_step_ == sizeof(initialization) / sizeof(*initialization)) {
        state_ = SensorState::Idle;
        if (requested_) { requested_ = false; request(now); }
        return;
    }
    const auto& step = initialization[initialize_step_];
    if (!update(step.reg, step.bit, step.set)) { fail(SensorFault::Bus); return; }
    ++initialize_step_;
    if (initialize_step_ == 3 || initialize_step_ == 9) {
        waiting_ = true; wait_deadline_ = now + (initialize_step_ == 3 ? 1000u : 2000u);
    }
}
void BoundedAcquisition::acquire(std::uint32_t now) {
    if (sample_step_ == 1 || sample_step_ == 7) {
        if (!update(2, 7, sample_step_ == 7)) { fail(SensorFault::Bus); return; }
        ++sample_step_; deadline_ = now + 400; waiting_ = false; return;
    }
    if (!waiting_) { waiting_ = true; wait_deadline_ = now + 1000; }
    std::uint8_t ready = 0;
    if (!io_.read(io_.context, 0, ready)) { fail(SensorFault::Bus); return; }
    if (!(ready & 32u)) {
        if (due(now, wait_deadline_)) fail(SensorFault::ReadyTimeout);
        else deadline_ = now + 5;
        return;
    }
    std::uint32_t bits = 0;
    if (!io_.read24(io_.context, 0x12, bits) || bits > 0xffffffu || !update(0, 4, true)) { fail(SensorFault::Bus); return; }
    if (bits & 0x800000u) bits |= 0xff000000u;
    std::int32_t value = 0; std::memcpy(&value, &bits, sizeof(value));
    waiting_ = false;
    if (sample_step_ == 6) {
        if (value < 1) { fail(SensorFault::InvalidPressure); return; }
        staged_.pressure_raw = value;
    } else if (sample_step_ == 12) {
        staged_.temperature_raw = value;
        staged_.temperature_f = oem::Acquisition::temperature_fahrenheit(value);
        if (!std::isfinite(staged_.temperature_f)) { fail(SensorFault::InvalidTemperature); return; }
        if (!io_.battery(io_.context, staged_.battery_v) || !std::isfinite(staged_.battery_v) || staged_.battery_v < 0) { fail(SensorFault::InvalidBattery); return; }
        staged_.measured_at = now; staged_.sequence = sample_.sequence + 1;
        if (!staged_.sequence) ++staged_.sequence;
        staged_.valid = true; sample_ = staged_; state_ = SensorState::Idle; return;
    }
    ++sample_step_; deadline_ = now + 100;
}
void BoundedAcquisition::poll(std::uint32_t now) {
    if (!due(now, deadline_)) return;
    if (state_ == SensorState::Initializing) initialize(now);
    else if (state_ == SensorState::Sampling) acquire(now);
}
}}
