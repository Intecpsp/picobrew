// Shared diagnostic adapter for the recovered PicoStill bodies. A completely
// empty sink preserves the original standalone value-only API; assembled
// firmware binds both callbacks and observes every returning assertion.
#pragma once
#include "zseries/console.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstddef>

namespace zseries { namespace oem { namespace detail {
class StillDiagnosticSink {
public:
    StillDiagnosticSink(const ConsoleServices* services, ConsoleResult& result,
                        std::uint32_t& assertion_line) noexcept
        : services_(services), result_(result), assertion_line_(assertion_line) {
        result_ = ConsoleResult::Ok;
    }
    bool enabled() const noexcept {
        return services_ && (services_->assertion || services_->log);
    }
    bool assertion(const char* file, std::uint32_t line, const char* expression) {
        assertion_line_ = line;
        if (!enabled()) return true;
        if (!services_->assertion) return missing();
        if (!services_->assertion(services_->context, file, line, expression)) return failed();
        return true;
    }
    bool log(ConsoleLevel level, const char* tag, const char* file,
             std::uint32_t line, const char* message) {
        if (!enabled()) return true;
        if (!services_->log) return missing();
        if (!services_->log(services_->context, level, tag, file, line, message)) return failed();
        return true;
    }
    bool print(ConsoleLevel level, const char* tag, const char* file,
               std::uint32_t line, const char* format, ...) {
        if (!enabled()) return true;
        char message[512];
        va_list args; va_start(args, format);
        const int count = ::vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        if (count < 0 || static_cast<std::size_t>(count) >= sizeof(message)) return failed();
        return log(level, tag, file, line, message);
    }
    // Buffer<T>::append reports truncation before copying its retained prefix.
    bool truncate(bool& truncated, std::size_t requested, std::size_t room) {
        if (requested <= room) return true;
        if (!truncated) {
            truncated = true;
            if (!log(ConsoleLevel::Warning,"BUF","Buffer.h",0x66,"Buffer truncated")) return false;
        } else if (room != 0 && !assertion("Buffer.h",0x68,"count == 0")) return false;
        return print(ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,
                     "Dropping %d bytes",static_cast<int>(requested-room));
    }
private:
    const ConsoleServices* services_;
    ConsoleResult& result_;
    std::uint32_t& assertion_line_;
    bool missing() noexcept { result_=ConsoleResult::MissingService;return false; }
    bool failed() noexcept { result_=ConsoleResult::ServiceFailure;return false; }
};
}}} // namespace zseries::oem::detail
