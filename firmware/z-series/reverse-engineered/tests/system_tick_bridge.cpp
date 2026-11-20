#include "zseries/system_tick.hpp"
#include <cstring>
#include <vector>

using namespace zseries::oem;
namespace {
struct Context {
    std::int32_t watchdog;
    std::uint32_t timestamp;
    bool log_success;
    bool assertion_success;
    std::uint32_t assertions;
    std::vector<std::uint32_t> events;
};
}

extern "C" unsigned system_tick_case(std::uint32_t* words, std::uint64_t* average,
        std::uint32_t now, std::int32_t watchdog, std::uint32_t timestamp,
        unsigned missing, bool log_success, bool assertion_success,
        std::uint32_t* assertions, std::uint32_t* events, unsigned* count,
        std::int32_t* observed_error) {
    SystemLoopTiming state;
    state.last_date = words[0]; state.duration = words[1];
    state.minimum = words[2]; state.maximum = words[3];
    std::memcpy(&state.average, average, sizeof state.average);
    Context context{watchdog, timestamp, log_success, assertion_success, *assertions, {}};
    SystemTickServices services;
    services.context = &context;
    services.reset_watchdog = [](void* raw) {
        auto& c = *static_cast<Context*>(raw); c.events.push_back(1); return c.watchdog;
    };
    services.millis = [](void* raw) {
        auto& c = *static_cast<Context*>(raw);
        c.events.insert(c.events.end(), {2, c.timestamp}); return c.timestamp;
    };
    services.assertion = [](void* raw, SystemTickAssertion kind) {
        auto& c = *static_cast<Context*>(raw);
        if (!c.assertion_success) { c.events.push_back(6); return false; }
        const std::uint32_t line = kind == SystemTickAssertion::WatchdogReset ? 0x106u : 0x2fu;
        c.events.insert(c.events.end(), {2, c.timestamp, 4, c.timestamp, line});
        ++c.assertions;
        c.events.insert(c.events.end(), {5, 200}); c.timestamp += 200;
        return true;
    };
    services.log_maximum = [](void* raw, std::uint32_t when, std::uint32_t maximum) {
        auto& c = *static_cast<Context*>(raw);
        c.events.insert(c.events.end(), {3, when, maximum}); return c.log_success;
    };
    if (missing == 1) services.reset_watchdog = nullptr;
    if (missing == 2) services.millis = nullptr;
    if (missing == 3) services.log_maximum = nullptr;
    if (missing == 4) services.assertion = nullptr;
    const auto result = tick_system(state, now, services, observed_error);
    words[0] = state.last_date; words[1] = state.duration;
    words[2] = state.minimum; words[3] = state.maximum;
    std::memcpy(average, &state.average, sizeof state.average);
    *assertions = context.assertions;
    *count = static_cast<unsigned>(context.events.size());
    for (unsigned i = 0; i < *count; ++i) events[i] = context.events[i];
    return static_cast<unsigned>(result);
}
