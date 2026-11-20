// Original Z119 System watchdog and SystemState loop timing.
#pragma once
#include <cstdint>

namespace zseries { namespace oem {

struct SystemLoopTiming {
    std::uint32_t last_date = 0;
    std::uint32_t duration = 0;
    std::uint32_t minimum = 0xffffffffu;
    std::uint32_t maximum = 0;
    double average = 0;
};

enum class SystemTickAssertion { WatchdogReset, ClockRegression };

struct SystemTickServices {
    void* context = nullptr;
    std::int32_t (*reset_watchdog)(void*) = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    // The OEM assertion helper logs, increments its global counter, waits
    // 200 ms and RETURNS. This required diagnostic binding must retain those
    // effects; a reported assertion does not stop the original timing update.
    bool (*assertion)(void*, SystemTickAssertion) = nullptr;
    // Original diagnostic occurs only for a new maximum strictly above 3000 ms.
    // The timestamp is the separate millis() sample, not the tick argument.
    bool (*log_maximum)(void*, std::uint32_t timestamp, std::uint32_t maximum) = nullptr;
};

enum class SystemTickResult { Ok, MissingService, AssertionServiceFailure, LogFailure };

// Original 0x400e5e2c, including continuation after both returning assertions.
// Unsigned subtraction after a clock-regression diagnostic intentionally wraps.
SystemTickResult tick_system(SystemLoopTiming&, std::uint32_t now,
                             const SystemTickServices&, std::int32_t* watchdog_error = nullptr);

}} // namespace zseries::oem
