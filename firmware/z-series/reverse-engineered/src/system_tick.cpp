#include "zseries/system_tick.hpp"

namespace zseries { namespace oem {

SystemTickResult tick_system(SystemLoopTiming& state, std::uint32_t now,
                            const SystemTickServices& services, std::int32_t* watchdog_error) {
    if (!services.reset_watchdog || !services.millis || !services.assertion || !services.log_maximum)
        return SystemTickResult::MissingService;

    const std::int32_t error = services.reset_watchdog(services.context);
    if (watchdog_error) *watchdog_error = error;
    if (error != 0 && !services.assertion(services.context, SystemTickAssertion::WatchdogReset))
        return SystemTickResult::AssertionServiceFailure;
    // The original helper returns after logging/delay, so a timer wrap is
    // diagnosed and still flows through the unsigned interval calculation.
    if (now < state.last_date && !services.assertion(services.context, SystemTickAssertion::ClockRegression))
        return SystemTickResult::AssertionServiceFailure;

    state.duration = now - state.last_date;
    if (state.duration < state.minimum) state.minimum = state.duration;
    const bool new_maximum = state.duration > state.maximum;
    if (new_maximum) state.maximum = state.duration;
    // Exact original IEEE-754 constants: 3feff7ced916872b and 3f50624dd2f1a9fc.
    // Keep separate multiply/add operations; the build disables contraction.
    const double previous = state.average * 0.999;
    const double current = static_cast<double>(state.duration) * 0.001;
    state.average = previous + current;
    state.last_date = now;

    if (new_maximum && state.maximum > 3000) {
        const std::uint32_t timestamp = services.millis(services.context);
        if (!services.log_maximum(services.context, timestamp, state.maximum))
            return SystemTickResult::LogFailure;
    }
    return SystemTickResult::Ok;
}

}} // namespace zseries::oem
