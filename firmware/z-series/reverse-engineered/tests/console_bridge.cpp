#include "zseries/console.hpp"
#include "zseries/serial_console.hpp"
#include "zseries/diagnostics.hpp"
#include <cstring>
#include <string>
#include <vector>

using namespace zseries::oem;
namespace {
std::vector<std::string> events, texts;
struct Context {
    std::uint32_t now = 0;
    DiagnosticState diagnostic;
    const std::uint8_t* bytes = nullptr;
    std::size_t length = 0, position = 0;
    TextCommandMailbox queue;
    ConsoleServices console;
};
std::uint32_t clock(void* opaque) { return static_cast<Context*>(opaque)->now; }
void assertion_log(void* opaque, double seconds, const char* file, std::uint32_t line, const char* expression) {
    auto& context = *static_cast<Context*>(opaque);
    std::uint64_t bits; std::memcpy(&bits, &seconds, sizeof(bits));
    events.push_back(std::string("A|") + file + "|" + std::to_string(line) + "|" + expression + "|" +
        std::to_string(static_cast<std::uint32_t>(bits)) + "|" + std::to_string(static_cast<std::uint32_t>(bits >> 32)) + "|" +
        std::to_string(context.diagnostic.assertion_count));
}
void delay_ms(void* opaque, std::uint32_t duration) {
    auto& context = *static_cast<Context*>(opaque);
    events.push_back("D|" + std::to_string(duration) + "|" + std::to_string(context.diagnostic.assertion_count));
    context.now += duration;
}
bool assertion(void* opaque, const char* file, std::uint32_t line, const char* expression) {
    DiagnosticServices services; services.context = opaque; services.millis = clock;
    services.assertion_log = assertion_log; services.delay_ms = delay_ms;
    return record_assertion(static_cast<Context*>(opaque)->diagnostic, services, file, line, expression) == DiagnosticResult::Ok;
}
bool log(void*, ConsoleLevel level, const char* tag, const char* file, std::uint32_t line, const char* message) {
    events.push_back("L|" + std::to_string(static_cast<unsigned>(level)) + "|" + tag + "|" + file + "|" +
        std::to_string(line) + "|" + message); return true;
}
bool command(void* opaque, const char* argument) {
    events.push_back("C|" + std::to_string(reinterpret_cast<std::uintptr_t>(opaque)) + "|" + argument); return true;
}
int available(void* opaque) {
    auto& context = *static_cast<Context*>(opaque); return static_cast<int>(context.length - context.position);
}
int read(void* opaque) {
    auto& context = *static_cast<Context*>(opaque); return context.bytes[context.position++];
}
bool post(void* opaque, const char* text) {
    auto& context = *static_cast<Context*>(opaque);
    const auto result = post_text_command(context.queue, text, "TextCommands", context.console);
    return result == TextCommandPostResult::Accepted || result == TextCommandPostResult::Discarded;
}
void commands(ConsoleShell& root, ConsoleShell& child, unsigned variant) {
    if (!variant) return;
    child.add_command(true, "run", "Run child", reinterpret_cast<void*>(5), command);
    child.add_command(false, "value ", "Set child value", reinterpret_cast<void*>(6), command);
    root.add_command(true, "status", "Read status", reinterpret_cast<void*>(1), command);
    root.add_command(false, "set ", "Set a value", reinterpret_cast<void*>(2), command);
    root.add_command(true, "set", "Set without arguments", reinterpret_cast<void*>(3), command);
    root.add_child("child ", child);
    root.add_command(false, "st", "Overlapping prefix", reinterpret_cast<void*>(4), command);
    if (variant == 2) {
        root.add_command(true, "?", "Override help", reinterpret_cast<void*>(7), command);
        root.add_command(false, "??", "Override recursive help", reinterpret_cast<void*>(8), command);
    }
}
}
extern "C" unsigned console_case(const std::uint32_t* data, const char* token, const char* description,
    const char* input, const std::uint8_t* bytes, std::size_t byte_count, std::uint32_t* output) {
    events.clear(); texts.clear();
    Context context; context.now = data[4]; context.diagnostic.assertion_count = data[5];
    context.console.context = &context; context.console.assertion = assertion; context.console.log = log;
    ConsoleEntry storage[8], children[8];
    ConsoleShell root("Root", storage, 8, context.console), child("Child", children, 8, context.console);
    unsigned result = 0;
    if (data[0] < 2) {
        for (unsigned i = 0; i < data[1]; ++i) root.add_command(true, "fill", "Filled", nullptr, command);
        if (!data[0]) result = static_cast<unsigned>(root.add_command(data[2], token, description,
            reinterpret_cast<void*>(9), command));
        else result = static_cast<unsigned>(root.add_child(token, data[3] ? nullptr : &child));
        for (std::size_t i = 0; i < root.size(); ++i) {
            const auto& entry = *root.entry(i);
            texts.push_back(std::string(entry.token) + "|" + (entry.description ? entry.description : "<null>") + "|" +
                std::to_string(entry.exact_match) + "|" + (entry.child ? entry.child->name() : ""));
        }
    } else if (data[0] < 4) {
        commands(root, child, data[1]);
        result = static_cast<unsigned>(data[0] == 2 ? root.dispatch(input) : root.help(data[2], 0, token));
    } else {
        context.queue.pending = data[7];
        if (description) std::memcpy(context.queue.text.data(), description, std::strlen(description));
        if (data[0] == 4) {
            SerialReaderState reader;
            if (token) std::memcpy(reader.text.data(), token, std::strlen(token));
            reader.length = token ? std::strlen(token) : 0; reader.remaining = 128 - reader.length;
            reader.frozen = data[2]; reader.truncated = data[9]; reader.non_ascii = data[8]; reader.invalid_lines = data[6];
            context.bytes = bytes; context.length = byte_count;
            SerialReaderServices services; services.context = &context; services.available = available;
            services.read = read; services.post_command = post; services.diagnostics = context.console;
            for (unsigned i = 0; i < data[3]; ++i) {
                result = static_cast<unsigned>(tick_serial_reader(reader, services));
                if (result) break;
            }
            output[4] = context.position; output[5] = reader.length; output[6] = reader.remaining;
            output[8] = reader.frozen; output[9] = reader.truncated; output[10] = reader.non_ascii;
            output[11] = reader.invalid_lines; texts.push_back(reader.text.data());
        } else result = static_cast<unsigned>(post_text_command(context.queue, input, "TextCommands", context.console));
        output[7] = context.queue.pending; texts.push_back(context.queue.text.data());
    }
    output[0] = result; output[1] = context.now; output[2] = context.diagnostic.assertion_count; output[3] = root.size();
    return result;
}
extern "C" unsigned console_event_count() { return events.size(); }
extern "C" const char* console_event(unsigned index) { return events.at(index).c_str(); }
extern "C" unsigned console_text_count() { return texts.size(); }
extern "C" const char* console_text(unsigned index) { return texts.at(index).c_str(); }
extern "C" unsigned console_guard_case(unsigned mode) {
    Context context; context.console.context = &context;
    context.console.assertion = assertion; context.console.log = log;
    ConsoleEntry entries[2]; ConsoleServices missing;
    ConsoleShell root("Guard", entries, 2, mode == 0 ? missing : context.console);
    if (mode == 0) return static_cast<unsigned>(root.add_command(true, "go", "Go", nullptr, command));
    if (mode == 1) return static_cast<unsigned>(root.add_command(true, "go", "Go", nullptr, nullptr));
    if (mode == 2) return static_cast<unsigned>(root.dispatch(nullptr));
    if (mode == 3) { root.add_child("child ", root); return static_cast<unsigned>(root.help(true)); }
    SerialReaderState reader; SerialReaderServices services;
    services.context = &context; services.available = available; services.read = read;
    services.post_command = post; services.diagnostics = context.console;
    if (mode == 4) reader.length = 129;
    if (mode == 5) services.post_command = nullptr;
    if (mode < 6) return static_cast<unsigned>(tick_serial_reader(reader, services));
    if (mode == 6) return static_cast<unsigned>(post_text_command(context.queue,
        std::string(129, 'x').c_str(), "TextCommands", context.console));
    std::array<char, 129> output{}; output[0] = 'k';
    if (take_text_command(context.queue, output) || output[0] != 'k') return 1;
    post_text_command(context.queue, "owned command", "TextCommands", context.console);
    if (!take_text_command(context.queue, output) || context.queue.pending ||
        std::strcmp(output.data(), "owned command") || std::strcmp(context.queue.text.data(), "owned command")) return 2;
    return 0;
}
