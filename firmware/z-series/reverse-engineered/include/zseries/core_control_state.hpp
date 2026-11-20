// Owned binding of recovered control observations into Runtime's inputs.
#pragma once
#include "zseries/program_runtime_heater.hpp"
#include "zseries/safeties.hpp"

namespace zseries { namespace oem {

// Call after the Core sensor/safety services and before Runtime::tick. The
// original objects share these flags through CoreState; the portable types
// use an explicit snapshot. Does not change unrelated network/PicoStill state.
void refresh_runtime_controls(ProgramRuntime&, const HeaterController&,
                              const HeaterSensors&, const SafetyState&) noexcept;

}} // namespace zseries::oem
