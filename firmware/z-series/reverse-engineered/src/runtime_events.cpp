#include "zseries/runtime_events.hpp"

namespace zseries { namespace oem {

CoreRuntimeEvent runtime_event_payload(RuntimeEventType type, std::uint32_t first,
                                       std::uint32_t second) noexcept {
    CoreRuntimeEvent event;
    event.type = type;
    if (type == RuntimeEventType::Notification) {
        event.notification = static_cast<RuntimeNotificationType>(first);
    } else if (type == RuntimeEventType::Transition) {
        event.previous = first;
        event.current = second;
    }
    return event;
}

bool post_runtime_event(CoreFlow& core, RuntimeEventType type, std::uint32_t first,
                        std::uint32_t second) noexcept {
    return core.runtime_events.put(runtime_event_payload(type, first, second));
}

HeadRuntimeEvent head_runtime_event(const CoreRuntimeEvent& event) noexcept {
    HeadRuntimeEvent result;
    result.type = event.type;
    result.notification = event.notification;
    return result;
}

}} // namespace zseries::oem
