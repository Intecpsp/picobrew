#pragma once
#include "zseries/program_runtime.hpp"
#include "zseries/heater.hpp"

namespace zseries { namespace oem {

// Owned integration glue. Keep the binding, runtime, controller and sensors
// alive while installed. It preserves the application's other service context.
struct RuntimeHeaterBinding {
    ProgramRuntime* runtime = nullptr;
    HeaterController* heater = nullptr;
    HeaterSensors* sensors = nullptr;
    HeaterIo io{nullptr, nullptr, nullptr};
};

bool bind_runtime_heater(ProgramRuntime&, RuntimeHeaterBinding&, HeaterController&,
                         HeaterSensors&, const HeaterIo&) noexcept;

// The application refreshes this snapshot after sensor/safety service work.
// Monitor delta flags, filter state and PicoStill observations are separate.
void refresh_runtime_thermal_state(ProgramRuntime&, const HeaterController&,
                                   const HeaterSensors&, const ThermalSafety&) noexcept;
}} // namespace zseries::oem
