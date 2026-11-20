#include "buzzer_platform.hpp"
#include <Arduino.h>
#include <cstdlib>

namespace zseries { namespace oem { namespace esp32 {
namespace {
std::uint32_t clock_ms(void*) { return millis(); }
bool attach(void*, std::uint8_t pin, std::uint8_t channel) {
    ledcAttachPin(pin, channel); return true;
}
bool configure(void*, std::uint8_t channel, double frequency, std::uint8_t resolution) {
    ledcSetup(channel, frequency, resolution); return true;
}
bool write(void*, std::uint8_t channel, std::uint32_t duty) {
    ledcWrite(channel, duty); return true;
}
bool begin_console(void* context, const char* name) {
    const auto& bindings = *static_cast<BuzzerBindings*>(context);
    return bindings.begin_console && bindings.begin_console(bindings.context, name);
}
bool add_command(void* context, Buzzer& buzzer, const BuzzerCommandDefinition& definition) {
    const auto& bindings = *static_cast<BuzzerBindings*>(context);
    return bindings.add_command && bindings.add_command(bindings.context, buzzer, definition);
}
std::int32_t decimal_integer(void*, const char* text) { return std::atoi(text); }
bool assertion(void* context, std::uint32_t source_line) {
    const auto& bindings = *static_cast<BuzzerBindings*>(context);
    return bindings.assertion && bindings.assertion(bindings.context, source_line);
}
bool diagnostic(void* context, BuzzerDiagnostic event, std::uint32_t timestamp,
                const BuzzerState& state, bool alarm_enabled) {
    const auto& bindings = *static_cast<BuzzerBindings*>(context);
    return bindings.diagnostic && bindings.diagnostic(bindings.context, event, timestamp, state, alarm_enabled);
}
}
BuzzerServices buzzer_services(BuzzerBindings& bindings) noexcept {
    BuzzerServices result; result.context = &bindings;
    result.millis = clock_ms; result.attach = attach; result.configure = configure;
    result.write = write; result.begin_console = begin_console; result.add_command = add_command;
    result.decimal_integer = decimal_integer; result.assertion = assertion; result.diagnostic = diagnostic;
    return result;
}
}}} // namespace zseries::oem::esp32
