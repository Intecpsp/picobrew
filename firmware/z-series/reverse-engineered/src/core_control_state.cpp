#include "zseries/core_control_state.hpp"

namespace zseries { namespace oem {
void refresh_runtime_controls(ProgramRuntime& runtime, const HeaterController& heater,
                              const HeaterSensors& sensors, const SafetyState& safety) noexcept {
    refresh_runtime_thermal_state(runtime, heater, sensors, safety.thermal);
    runtime.observed.filter_absent = safety.filter_missing;
    runtime.observed.max_hex_wort_delta = safety.max_hex_wort_delta;
    runtime.observed.max_wort_drain_delta = safety.max_wort_drain_delta;
    runtime.observed.max_hex_rise = safety.max_hex_rise;
}
}} // namespace zseries::oem
