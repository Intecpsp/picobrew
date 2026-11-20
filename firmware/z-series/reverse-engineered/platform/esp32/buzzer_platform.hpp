// Historical Arduino LEDC operations with caller-owned console/log bindings.
#pragma once
#include "zseries/buzzer.hpp"

namespace zseries { namespace oem { namespace esp32 {
struct BuzzerBindings {
    void* context = nullptr;
    bool (*begin_console)(void*, const char*) = nullptr;
    bool (*add_command)(void*, Buzzer&, const BuzzerCommandDefinition&) = nullptr;
    bool (*assertion)(void*, std::uint32_t source_line) = nullptr;
    bool (*diagnostic)(void*, BuzzerDiagnostic, std::uint32_t timestamp,
                       const BuzzerState&, bool alarm_enabled) = nullptr;
};
// Bindings must outlive the returned services. The caller serializes access
// between the Core task, console callbacks and the FreeRTOS software timer;
// no successful no-op substitutes for a missing registry, assertion service
// or diagnostic sink. The assertion service must return after recording it.
BuzzerServices buzzer_services(BuzzerBindings&) noexcept;
}}} // namespace zseries::oem::esp32
