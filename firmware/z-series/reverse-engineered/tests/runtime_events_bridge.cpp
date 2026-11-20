#include "zseries/runtime_events.hpp"
using namespace zseries::oem;
extern "C" int runtime_events_mailbox(const std::uint32_t* input, std::uint32_t* output) {
    PendingEvent<CoreRuntimeEvent> mailbox;
    mailbox.value.type = static_cast<RuntimeEventType>(0x44440000u);
    mailbox.value.previous = 0x44440001u;
    mailbox.value.current = 0x44440002u;
    mailbox.value.notification = static_cast<RuntimeNotificationType>(0x44440003u);
    mailbox.pending = input[4] != 0;
    CoreRuntimeEvent event;
    event.type = static_cast<RuntimeEventType>(input[0]);
    event.previous = input[1];
    event.current = input[2];
    event.notification = static_cast<RuntimeNotificationType>(input[3]);
    (void)mailbox.put(event);
    output[4] = mailbox.pending;
    if (!mailbox.take(event)) return 1;
    output[0] = static_cast<std::uint32_t>(event.type);
    output[1] = event.previous;
    output[2] = event.current;
    output[3] = static_cast<std::uint32_t>(head_runtime_event(event).notification);
    output[5] = mailbox.pending;
    return 0;
}
