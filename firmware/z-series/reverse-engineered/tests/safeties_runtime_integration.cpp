#include "zseries/core_control_state.hpp"
#include <cassert>
using namespace zseries::oem;
namespace {
struct Input { int value = 0; unsigned configured = 0; };
void configure(void* p, std::uint8_t pin) { assert(pin == 36); ++static_cast<Input*>(p)->configured; }
int read(void* p, std::uint8_t pin) { assert(pin == 36); return static_cast<Input*>(p)->value; }
}
int main() {
    Input input;
    SafetyIo io{&input, configure, read};
    assert(initialize_safety_input(io) && input.configured == 1);
    io.configure_input = nullptr;
    assert(!initialize_safety_input(io) && input.configured == 1);
    SafetyController controller;
    SafetyState safety;
    HeaterController heater;
    HeaterSensors sensors;
    z_msp_state msp{};
    ProgramRuntime runtime;
    runtime.filter_required = true;
    sensors.temperatures[0].celsius = 20;
    sensors.temperatures[1].celsius = 20;
    sensors.temperatures[2].celsius = 20;
    assert(safety.thermal.enabled);
    assert(update_safeties(controller, safety, sensors, msp, 1000, io));
    refresh_runtime_controls(runtime, heater, sensors, safety);
    assert(runtime.pause_reason(RuntimeCommandId::None) == 0);
    sensors.temperatures[0].celsius = 40.001;
    assert(update_safeties(controller, safety, sensors, msp, 2000, io));
    refresh_runtime_controls(runtime, heater, sensors, safety);
    assert(runtime.pause_reason(RuntimeCommandId::None) == 8);
    msp.pumps = 16; msp.valve_position = 4;
    assert(update_safeties(controller, safety, sensors, msp, 3000, io));
    refresh_runtime_controls(runtime, heater, sensors, safety);
    assert(runtime.pause_reason(RuntimeCommandId::None) == 0);
    assert(update_safeties(controller, safety, sensors, msp, 8000, io));
    refresh_runtime_controls(runtime, heater, sensors, safety);
    assert(runtime.pause_reason(RuntimeCommandId::None) == 6);
    runtime.state.ignore_monitor_safeties = true;
    assert(runtime.pause_reason(RuntimeCommandId::None) == 0);
    input.value = 1;
    assert(update_safeties(controller, safety, sensors, msp, 9000, io));
    refresh_runtime_controls(runtime, heater, sensors, safety);
    assert(runtime.pause_reason(RuntimeCommandId::None) == 3);
    safety.thermal.too_cold = 4;
    refresh_runtime_controls(runtime, heater, sensors, safety);
    assert(runtime.pause_reason(RuntimeCommandId::None) == 5);
    safety.thermal.too_hot = 2;
    refresh_runtime_controls(runtime, heater, sensors, safety);
    assert(runtime.pause_reason(RuntimeCommandId::None) == 4);
    // The original timer independently gates stale sensor data. Runtime's
    // pause priority does not gain a fabricated too-old branch in this binding.
    safety.thermal.too_hot = safety.thermal.too_cold = 0;
    safety.thermal.too_old = 3; safety.filter_missing = false;
    refresh_runtime_controls(runtime, heater, sensors, safety);
    assert(runtime.pause_reason(RuntimeCommandId::None) == 0);
    return 0;
}
