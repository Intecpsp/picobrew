// Original Z119 returning assertion helper, 0x400db014.
#pragma once
#include <cstdint>

namespace zseries { namespace oem {

struct DiagnosticState { std::uint32_t assertion_count = 0; };
struct DiagnosticServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    void (*assertion_log)(void*, double seconds, const char* file,
                          std::uint32_t line, const char* expression) = nullptr;
    void (*delay_ms)(void*, std::uint32_t) = nullptr;
};
enum class DiagnosticResult { Ok, MissingService };

// Logging precedes the wrapping counter increment, which precedes delay(200).
// The helper returns; it does not throw, abort, halt or disable an actuator.
DiagnosticResult record_assertion(DiagnosticState&, const DiagnosticServices&,
                                  const char* file, std::uint32_t line,
                                  const char* expression) noexcept;

}} // namespace zseries::oem
