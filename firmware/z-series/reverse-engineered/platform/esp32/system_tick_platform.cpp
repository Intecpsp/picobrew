#include "system_tick_platform.hpp"
#include <Arduino.h>
#include <esp_task_wdt.h>

namespace zseries { namespace oem { namespace esp32 {

SystemTickServices system_tick_services(SystemTickBindings& bindings) noexcept {
    SystemTickServices services;
    services.context = &bindings;
    services.reset_watchdog = [](void*) { return static_cast<std::int32_t>(esp_task_wdt_reset()); };
    services.millis = [](void*) { return static_cast<std::uint32_t>(::millis()); };
    if (bindings.assertion) {
        services.assertion = [](void* context, SystemTickAssertion assertion) {
            const auto& value = *static_cast<SystemTickBindings*>(context);
            return value.assertion && value.assertion(value.context, assertion);
        };
    }
    if (bindings.log_maximum) {
        services.log_maximum = [](void* context, std::uint32_t timestamp, std::uint32_t maximum) {
            const auto& value = *static_cast<SystemTickBindings*>(context);
            return value.log_maximum && value.log_maximum(value.context, timestamp, maximum);
        };
    }
    return services;
}

}}} // namespace zseries::oem::esp32
