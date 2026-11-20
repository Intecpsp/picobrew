#include "zseries/serial_console.hpp"
#include <cstdio>
#include <cstring>
#include <string>

namespace zseries { namespace oem {
SerialReaderResult tick_serial_reader(SerialReaderState& state, const SerialReaderServices& services) {
    const auto& diagnostics = services.diagnostics;
    if (!services.available || !services.read || !services.post_command ||
        !diagnostics.assertion || !diagnostics.log) return SerialReaderResult::MissingService;
    if (state.length > 128 || state.remaining != 128 - state.length || state.text[state.length])
        return SerialReaderResult::InvalidState;
    for (;;) {
        if (!services.available(services.context)) return SerialReaderResult::Ok;
        const auto byte = static_cast<std::uint8_t>(services.read(services.context));
        if (byte == 0 || byte == '\r' || byte == '\n') break;
        if (state.frozen && !diagnostics.assertion(diagnostics.context, "Buffer.h", 154, "!_isFrozen"))
            return SerialReaderResult::ServiceFailure;
        if (!state.frozen) {
            if (!state.remaining) {
                if (!state.truncated) {
                    state.truncated = true;
                    if (!diagnostics.log(diagnostics.context, ConsoleLevel::Warning,
                        "BUF", "Buffer.h", 163, "Buffer truncated")) return SerialReaderResult::ServiceFailure;
                }
            } else {
                state.text[state.length++] = static_cast<char>(byte);
                --state.remaining;
                state.text[state.length] = 0;
            }
        }
        state.non_ascii = state.non_ascii || byte < 0x20 || byte > 0x7e;
        if (!state.remaining) break;
    }
    if (!state.length) {
        if (!diagnostics.log(diagnostics.context, ConsoleLevel::Warning,
            "ISER", "SerialReader.h", 80, "Empty input")) return SerialReaderResult::ServiceFailure;
    } else if (state.non_ascii) {
        ++state.invalid_lines;
        if (!diagnostics.log(diagnostics.context, ConsoleLevel::Warning,
            "ISER", "SerialReader.h", 83, "Non ascii input")) return SerialReaderResult::ServiceFailure;
    } else if (!services.post_command(services.context, state.text.data())) return SerialReaderResult::ServiceFailure;
    state.text.fill(0); state.length = 0; state.remaining = 128;
    state.truncated = false; state.frozen = false; state.non_ascii = false;
    return SerialReaderResult::Ok;
}
TextCommandPostResult post_text_command(TextCommandMailbox& queue, const char* command,
    const char* queue_name, const ConsoleServices& services) {
    if (!command || !queue_name || std::strlen(command) > 128) return TextCommandPostResult::InvalidInput;
    if (!services.log) return TextCommandPostResult::MissingService;
    if (!queue.pending) {
        queue.text.fill(0);
        std::memcpy(queue.text.data(), command, std::strlen(command));
        queue.pending = true;
        return TextCommandPostResult::Accepted;
    }
    const auto prefix = std::string("[") + queue_name + "] ";
    auto message = prefix + "EventQ::put() called while isEmpty() is false";
    if (!services.log(services.context, ConsoleLevel::Error, "EVTQ", "EventQ.h", 56, message.c_str()))
        return TextCommandPostResult::ServiceFailure;
    message = prefix + "Pending: " + std::string(queue.text.data()).substr(0, 127);
    if (!services.log(services.context, ConsoleLevel::Error, "EVTQ", "EventQ.h", 57, message.c_str()))
        return TextCommandPostResult::ServiceFailure;
    message = prefix + "Discarded: " + std::string(command).substr(0, 127);
    return services.log(services.context, ConsoleLevel::Error, "EVTQ", "EventQ.h", 60, message.c_str()) ?
        TextCommandPostResult::Discarded : TextCommandPostResult::ServiceFailure;
}
bool take_text_command(TextCommandMailbox& queue, std::array<char, 129>& output) noexcept {
    if (!queue.pending) return false;
    output = queue.text; queue.pending = false; return true;
}
}} // namespace zseries::oem
