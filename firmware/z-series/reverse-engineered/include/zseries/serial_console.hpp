#pragma once
#include "zseries/console.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {
struct SerialReaderState {
    std::array<char, 129> text{};
    std::uint32_t length = 0, remaining = 128;
    bool truncated = false, frozen = false, non_ascii = false;
    std::uint32_t invalid_lines = 0;
};
struct SerialReaderServices {
    void* context = nullptr;
    int (*available)(void*) = nullptr;
    int (*read)(void*) = nullptr;
    // Must copy the command before return. A full one-slot mailbox deliberately
    // discards the new command; that is not an adapter failure.
    bool (*post_command)(void*, const char*) = nullptr;
    ConsoleServices diagnostics{};
};
enum class SerialReaderResult { Ok, MissingService, ServiceFailure, InvalidState };
// One invocation stops at the first delimiter/full buffer, leaving later bytes
// for the next Core tick. Incomplete lines remain buffered between invocations.
SerialReaderResult tick_serial_reader(SerialReaderState&, const SerialReaderServices&);

struct TextCommandMailbox {
    std::array<char, 129> text{};
    bool pending = false;
};
enum class TextCommandPostResult { Accepted, Discarded, MissingService, ServiceFailure, InvalidInput };
TextCommandPostResult post_text_command(TextCommandMailbox&, const char* command,
    const char* queue_name, const ConsoleServices&);
// A successful take clears only pending. The payload remains until overwritten.
bool take_text_command(TextCommandMailbox&, std::array<char, 129>& output) noexcept;
}} // namespace zseries::oem
