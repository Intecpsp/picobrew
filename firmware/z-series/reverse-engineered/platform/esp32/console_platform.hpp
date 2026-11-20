#pragma once
#include "zseries/console.hpp"
#include "zseries/serial_console.hpp"
#include "zseries/diagnostics.hpp"
#include <Arduino.h>

namespace zseries { namespace oem { namespace esp32 {
struct ConsoleBindings {
    Stream* input = nullptr;
    Print* output = nullptr;
    TextCommandMailbox* commands = nullptr;
    const char* queue_name = nullptr;
    DiagnosticState* diagnostics = nullptr;
    DiagnosticServices diagnostic_services{};
};
// Bindings and all pointed-to objects are owned by the application and must
// outlive the shell/reader. No private queue or successful no-op sink is used.
ConsoleServices console_services(ConsoleBindings&) noexcept;
SerialReaderServices serial_reader_services(ConsoleBindings&) noexcept;
}}} // namespace zseries::oem::esp32
