#include "console_platform.hpp"

namespace zseries { namespace oem { namespace esp32 {
namespace {
bool assertion(void* opaque, const char* file, std::uint32_t line, const char* expression) {
    auto& binding = *static_cast<ConsoleBindings*>(opaque);
    return binding.diagnostics && record_assertion(*binding.diagnostics,
        binding.diagnostic_services, file, line, expression) == DiagnosticResult::Ok;
}
bool log(void* opaque, ConsoleLevel level, const char* tag, const char* file,
    std::uint32_t line, const char* message) {
    auto& binding = *static_cast<ConsoleBindings*>(opaque);
    if (!binding.output) return false;
    const auto seconds = static_cast<double>(static_cast<float>(::millis()) / 1000.0f);
    // Value/information records have the original IV prefix and no source
    // location; ordinary diagnostics keep their source-qualified I_ prefix.
    if (level == ConsoleLevel::Info && !file)
        return binding.output->printf("[IV][%8.2f][%s] %s\r\n", seconds, tag, message) > 0;
    const char* color = level == ConsoleLevel::Error ? "\x1b[31m" :
        level == ConsoleLevel::Warning ? "\x1b[33m" : "";
    const char* code = level == ConsoleLevel::Error ? "E_" :
        level == ConsoleLevel::Warning ? "W_" : level == ConsoleLevel::Debug ? "D_" : "I_";
    return binding.output->printf("%s[%s][%8.2f][%s][%s:%u] %s%s\r\n", color,
        code, seconds, tag, file, static_cast<unsigned>(line), message, *color ? "\x1b[0m" : "") > 0;
}
int available(void* opaque) {
    auto& binding = *static_cast<ConsoleBindings*>(opaque);
    return binding.input->available();
}
int read(void* opaque) { return static_cast<ConsoleBindings*>(opaque)->input->read(); }
bool post(void* opaque, const char* text) {
    auto& binding = *static_cast<ConsoleBindings*>(opaque);
    if (!binding.commands || !binding.queue_name) return false;
    const auto result = post_text_command(*binding.commands, text, binding.queue_name, console_services(binding));
    return result == TextCommandPostResult::Accepted || result == TextCommandPostResult::Discarded;
}
}
ConsoleServices console_services(ConsoleBindings& binding) noexcept {
    ConsoleServices services; services.context = &binding;
    if (binding.diagnostics) services.assertion = assertion;
    if (binding.output) services.log = log;
    return services;
}
SerialReaderServices serial_reader_services(ConsoleBindings& binding) noexcept {
    SerialReaderServices services; services.context = &binding;
    if (binding.input) { services.available = available; services.read = read; }
    if (binding.commands && binding.queue_name) services.post_command = post;
    services.diagnostics = console_services(binding); return services;
}
}}} // namespace zseries::oem::esp32
