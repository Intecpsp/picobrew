// Actual historical ESP32 watchdog/clock binding with an explicit log sink.
#pragma once
#include "zseries/system_tick.hpp"

namespace zseries { namespace oem { namespace esp32 {

struct SystemTickBindings {
    void* context = nullptr;
    bool (*assertion)(void*, SystemTickAssertion) = nullptr;
    bool (*log_maximum)(void*, std::uint32_t, std::uint32_t) = nullptr;
};

// The caller-owned binding and diagnostic sink must outlive these services.
SystemTickServices system_tick_services(SystemTickBindings&) noexcept;

}}} // namespace zseries::oem::esp32
