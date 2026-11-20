#include "zseries/program_runtime_heater.hpp"

namespace zseries { namespace oem {
namespace {
RuntimeHeaterBinding& binding(void* context) noexcept { return *static_cast<RuntimeHeaterBinding*>(context); }
double wort(const RuntimeHeaterBinding& port) noexcept {
    return port.sensors->temperatures[static_cast<std::size_t>(TempSensorId::Wort)].celsius;
}
bool enable(void* context, bool value) {
    auto& port = binding(context);
    return heater_enable(*port.heater, port.io, value);
}
bool start(void* context) {
    auto& port = binding(context);
    heater_start(*port.heater, wort(port));
    return true;
}
bool stop(void* context) {
    auto& port = binding(context);
    return heater_stop(*port.heater, port.io, wort(port));
}
bool target(void* context, double requested) {
    auto& port = binding(context);
    if (!heater_set_target(*port.heater, requested, port.sensors->estimated_boil_celsius)) return false;
    port.runtime->observed.heater_target = port.heater->state.target_celsius;
    return true;
}
}

bool bind_runtime_heater(ProgramRuntime& runtime, RuntimeHeaterBinding& port, HeaterController& heater,
                         HeaterSensors& sensors, const HeaterIo& io) noexcept {
    if (!io.write_pin) return false;
    port.runtime = &runtime;
    port.heater = &heater;
    port.sensors = &sensors;
    port.io = io;
    runtime.services.heater_context = &port;
    runtime.services.heater_enable = enable;
    runtime.services.pid_start = start;
    runtime.services.pid_stop = stop;
    runtime.services.heater_target = target;
    runtime.observed.heater_target = heater.state.target_celsius;
    return true;
}

void refresh_runtime_thermal_state(ProgramRuntime& runtime, const HeaterController& heater,
                                   const HeaterSensors& sensors, const ThermalSafety& safety) noexcept {
    runtime.observed.heater_target = heater.state.target_celsius;
    runtime.observed.hex_temperature = sensors.temperatures[static_cast<std::size_t>(TempSensorId::Hex)].celsius;
    runtime.observed.wort_temperature = sensors.temperatures[static_cast<std::size_t>(TempSensorId::Wort)].celsius;
    runtime.observed.drain_temperature = sensors.temperatures[static_cast<std::size_t>(TempSensorId::Drain)].celsius;
    runtime.observed.wort_rate = sensors.wort_rate;
    runtime.observed.too_hot = safety.too_hot != 0;
    runtime.observed.too_cold = safety.too_cold != 0;
}
}} // namespace zseries::oem
