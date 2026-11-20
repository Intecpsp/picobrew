#include "zseries/diagnostics.hpp"

namespace zseries { namespace oem {
DiagnosticResult record_assertion(DiagnosticState& state, const DiagnosticServices& services,
                                  const char* file, std::uint32_t line,
                                  const char* expression) noexcept {
    if (!services.millis || !services.assertion_log || !services.delay_ms)
        return DiagnosticResult::MissingService;
    const std::uint32_t timestamp = services.millis(services.context);
    // The original converts uint32 -> float, divides by float 1000, and only
    // then widens to double for the variadic diagnostic formatter.
    const float seconds = static_cast<float>(timestamp) / 1000.0f;
    services.assertion_log(services.context, static_cast<double>(seconds), file, line, expression);
    ++state.assertion_count;
    services.delay_ms(services.context, 200);
    return DiagnosticResult::Ok;
}
}} // namespace zseries::oem
