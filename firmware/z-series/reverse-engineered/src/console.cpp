#include "zseries/console.hpp"
#include <cstdio>
#include <cstring>
#include <string>

namespace zseries { namespace oem {
namespace {
bool empty(const char* value) { return !value || !*value; }
const char* printable(const char* value) { return value ? value : "(null)"; }
bool starts_with(const char* input, const char* prefix) {
    return std::strncmp(input, prefix, std::strlen(prefix)) == 0;
}
std::string label(const char* prefix, const char* token, bool exact) {
    // Original help uses these two separate bounded snprintf operations.
    const auto combined = (std::string(prefix) + token).substr(0, 127);
    auto text = ("- `" + combined + (exact ? "" : "ARGS") + "`").substr(0, 127);
    if (text.size() < 25) text.append(25 - text.size(), ' ');
    return text;
}
}
ConsoleShell::ConsoleShell(const char* name, ConsoleEntry* entries, std::size_t capacity,
    const ConsoleServices& services) noexcept : name_(name), entries_(entries),
    capacity_(entries ? capacity : 0), services_(services) {}
bool ConsoleShell::valid_services() const noexcept { return services_.assertion && services_.log; }
bool ConsoleShell::diagnose(std::uint32_t line, const char* expression) {
    return services_.assertion(services_.context, "Shell.h", line, expression);
}
bool ConsoleShell::log(ConsoleLevel level, const char* tag, std::uint32_t line, const char* message) {
    return services_.log(services_.context, level, tag, "Shell.h", line, message);
}
ConsoleResult ConsoleShell::rejected_trigger(const char* token, bool child) {
    // CString::copy's diagnostic temporary has capacity 32 including NUL.
    char copy[32] = {};
    if (token) ::snprintf(copy, sizeof(copy), "%s", token);
    std::string warning;
    std::uint32_t line = 0;
    if (!token) { line = 87; warning = "Invalid arg: src is NULL"; }
    else if (!*token) { line = 91; warning = "Invalid arg: srclen <= 0"; }
    else if (std::strlen(token) >= sizeof(copy)) {
        line = 99; warning = std::string("String truncated on copy \"") + copy + "\"";
    }
    if (line && !services_.log(services_.context, ConsoleLevel::Warning,
        "CSTR", "CString.h", line, warning.c_str())) return ConsoleResult::ServiceFailure;
    const auto message = std::string(child ? "Cannot add subshell trigger \"" : "Cannot add trigger \"") +
        copy + "\" to shell \"" + printable(name_) + "\"";
    return log(ConsoleLevel::Error, "SHELL", child ? 65 : 85, message.c_str()) ?
        ConsoleResult::Rejected : ConsoleResult::ServiceFailure;
}
ConsoleResult ConsoleShell::add_command(bool exact_match, const char* token,
    const char* description, void* context, ConsoleCallback invoke) {
    if (!valid_services()) return ConsoleResult::MissingService;
    if (size_ >= capacity_ && !diagnose(79, "_commandsCount < SIZE")) return ConsoleResult::ServiceFailure;
    if (empty(token) && !diagnose(80, "!CString::isNullOrEmpty(trigger)")) return ConsoleResult::ServiceFailure;
    if (empty(description) && !diagnose(81, "!CString::isNullOrEmpty(description)")) return ConsoleResult::ServiceFailure;
    if (size_ >= capacity_ || empty(token)) {
        return rejected_trigger(token, false);
    }
    // A missing C++ callable in the OEM throws bad_function_call on dispatch.
    // This no-exception rebuild rejects it explicitly instead of registering it.
    if (!invoke) return ConsoleResult::InvalidInput;
    auto& entry = entries_[size_++];
    entry.token = token; entry.description = description; entry.context = context;
    entry.invoke = invoke; entry.child = nullptr; entry.exact_match = exact_match;
    return ConsoleResult::Ok;
}
ConsoleResult ConsoleShell::add_child(const char* token, ConsoleShell* child) {
    if (!valid_services()) return ConsoleResult::MissingService;
    if (!child && !diagnose(59, "shell != nullptr")) return ConsoleResult::ServiceFailure;
    if (size_ >= capacity_ && !diagnose(60, "_commandsCount < SIZE")) return ConsoleResult::ServiceFailure;
    if (empty(token) && !diagnose(61, "!CString::isNullOrEmpty(trigger)")) return ConsoleResult::ServiceFailure;
    if (!child || size_ >= capacity_ || empty(token)) {
        return rejected_trigger(token, true);
    }
    auto& entry = entries_[size_++];
    entry.token = token; entry.description = nullptr; entry.context = nullptr;
    entry.invoke = nullptr; entry.child = child; entry.exact_match = false;
    return ConsoleResult::Ok;
}
ConsoleResult ConsoleShell::dispatch(const char* input) { return dispatch_at(input, 0); }
ConsoleResult ConsoleShell::dispatch_at(const char* input, std::uint32_t depth) {
    if (!valid_services()) return ConsoleResult::MissingService;
    if (!input) return ConsoleResult::InvalidInput;
    // Actual application trees have finite depth. Reject cyclic/malformed graphs
    // instead of reproducing unbounded recursion and stack exhaustion.
    if (depth >= 64) return ConsoleResult::InvalidGraph;
    for (std::size_t i = 0; i < size_; ++i) {
        const auto& entry = entries_[i];
        const bool match = entry.exact_match ? std::strcmp(input, entry.token) == 0 : starts_with(input, entry.token);
        if (!match) continue;
        const char* argument = input + std::strlen(entry.token);
        if (entry.child) return entry.child->dispatch_at(argument, depth + 1);
        return entry.invoke(entry.context, argument) ? ConsoleResult::Ok : ConsoleResult::ServiceFailure;
    }
    if (!starts_with(input, "?")) {
        const auto message = std::string("Failed to parse command(") + input + ")";
        if (!log(ConsoleLevel::Error, "ZSER", 168, message.c_str())) return ConsoleResult::ServiceFailure;
    }
    return help(starts_with(input, "??"));
}
ConsoleResult ConsoleShell::help(bool recursive, std::uint32_t depth, const char* prefix) {
    if (!valid_services()) return ConsoleResult::MissingService;
    if (!prefix) return ConsoleResult::InvalidInput;
    if (depth >= 64) return ConsoleResult::InvalidGraph;
    const char* banner = "*****************************************************************";
    if (!log(ConsoleLevel::Info, "SHELL", 104, banner)) return ConsoleResult::ServiceFailure;
    char count[32]; ::snprintf(count, sizeof(count), "%lu", static_cast<unsigned long>(size_ + 2));
    const auto title = std::string("# ") + printable(name_) + " Command List (" + count + ")";
    if (!log(ConsoleLevel::Info, "SHELL", 105, title.c_str()) ||
        !log(ConsoleLevel::Info, "SHELL", 106, banner)) return ConsoleResult::ServiceFailure;
    auto line = label(prefix, "?", true) + " Display all \"" + printable(name_) + "\" commands (ie. this list)";
    if (!log(ConsoleLevel::Info, "SHELL", 113, line.c_str())) return ConsoleResult::ServiceFailure;
    line = label(prefix, "??", true) + " Display all \"" + printable(name_) + "\" commands and all sub-shell commands (ie. recursive `?`)";
    if (!log(ConsoleLevel::Info, "SHELL", 117, line.c_str())) return ConsoleResult::ServiceFailure;
    for (std::size_t i = 0; i < size_; ++i) {
        const auto& entry = entries_[i];
        line = label(prefix, entry.token, entry.exact_match) + " ";
        line += entry.child ? std::string("Forward ARGS to sub shell ") + printable(entry.child->name()) : printable(entry.description);
        if (!log(ConsoleLevel::Info, "SHELL", entry.child ? 129 : 124, line.c_str())) return ConsoleResult::ServiceFailure;
    }
    if (recursive) {
        for (std::size_t i = 0; i < size_; ++i) {
            const auto& entry = entries_[i];
            if (!entry.child) continue;
            char combined[128];
            ::snprintf(combined, sizeof(combined), "%s%s", prefix, entry.token);
            const auto result = entry.child->help(true, depth + 1, combined);
            if (result != ConsoleResult::Ok) return result;
        }
    }
    return ConsoleResult::Ok;
}
}} // namespace zseries::oem
