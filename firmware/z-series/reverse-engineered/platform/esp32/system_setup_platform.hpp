#pragma once
#include "esp32_platform.hpp"
#include "zseries/diagnostics.hpp"
#include "zseries/system_setup.hpp"

namespace zseries { namespace oem { namespace esp32 {
struct SystemSetupBindings {
    System* system = nullptr;
    DiagnosticState* diagnostics = nullptr;
    DiagnosticServices diagnostic_services;
    void* console_context = nullptr;
    // Must register all descriptors plus the original "b " Buzzer namespace.
    bool (*register_console)(void*, const std::array<SystemConsoleDescriptor,7>&,
                             const char* buzzer_token) = nullptr;
};
SystemSetupServices system_setup_services(SystemSetupBindings&) noexcept;
// Actual clock, Serial diagnostic output and delay calls. Counter storage is
// application-owned and is shared by all original diagnostic bindings.
DiagnosticServices serial_diagnostic_services() noexcept;
}}} // namespace zseries::oem::esp32
