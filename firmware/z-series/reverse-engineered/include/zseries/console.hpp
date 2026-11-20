// Z119 Shell registration, dispatch and recursive command help.
#pragma once
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {
enum class ConsoleLevel : std::uint8_t { Info, Warning, Error, Debug };
struct ConsoleServices {
    void* context = nullptr;
    bool (*assertion)(void*, const char* file, std::uint32_t line, const char* expression) = nullptr;
    bool (*log)(void*, ConsoleLevel, const char* tag, const char* file,
                std::uint32_t line, const char* message) = nullptr;
};
enum class ConsoleResult { Ok, Rejected, MissingService, ServiceFailure, InvalidInput, InvalidGraph };
class ConsoleShell;
using ConsoleCallback = bool (*)(void*, const char* argument);
struct ConsoleEntry {
    const char* token = nullptr;
    const char* description = nullptr;
    void* context = nullptr;
    ConsoleCallback invoke = nullptr;
    ConsoleShell* child = nullptr;
    bool exact_match = false;
};

// The name, entry buffer, command strings, contexts and children are borrowed
// for this shell's lifetime. No references to temporary command envelopes.
class ConsoleShell {
public:
    ConsoleShell(const char* name, ConsoleEntry* entries, std::size_t capacity,
                 const ConsoleServices& services) noexcept;
    ConsoleResult add_command(bool exact_match, const char* token,
        const char* description, void* context, ConsoleCallback invoke);
    ConsoleResult add_child(const char* token, ConsoleShell* child);
    ConsoleResult add_child(const char* token, ConsoleShell& child) { return add_child(token, &child); }
    ConsoleResult dispatch(const char* input);
    ConsoleResult help(bool recursive = false, std::uint32_t depth = 0, const char* prefix = "");
    const char* name() const noexcept { return name_; }
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return capacity_; }
    const ConsoleEntry* entry(std::size_t index) const noexcept { return index < size_ ? entries_ + index : nullptr; }
private:
    const char* name_;
    ConsoleEntry* entries_;
    std::size_t capacity_, size_ = 0;
    ConsoleServices services_;
    bool valid_services() const noexcept;
    bool diagnose(std::uint32_t line, const char* expression);
    bool log(ConsoleLevel, const char* tag, std::uint32_t line, const char* message);
    ConsoleResult rejected_trigger(const char* token, bool child);
    ConsoleResult dispatch_at(const char* input, std::uint32_t depth);
};
}} // namespace zseries::oem
