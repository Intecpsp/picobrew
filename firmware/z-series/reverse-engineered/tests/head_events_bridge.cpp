#include "zseries/head_events.hpp"
#include <vector>

using namespace zseries::oem;
static bool record(void* context, HeadEventEffect effect, std::uint32_t argument) {
    auto& events = *static_cast<std::vector<std::uint32_t>*>(context);
    events.push_back(static_cast<unsigned>(effect)); events.push_back(argument); return true;
}
extern "C" int head_events_test(unsigned operation, const std::uint32_t* input,
                               std::uint32_t* state, std::uint32_t* output) {
    HeadEvents head;
    head.state.runtime = static_cast<RuntimeStateId>(input[0]);
    head.state.instruction = static_cast<OpCode>(input[1]);
    head.state.pause = static_cast<PauseReason>(input[2]);
    head.state.screen = static_cast<ScreenId>(input[3]);
    head.state.notification = static_cast<RuntimeNotificationType>(input[4]);
    head.state.encoder_press_hijacked = input[5] != 0;
    std::vector<std::uint32_t> events;
    head.services.context = &events; head.services.perform = record;
    HeadRuntimeEvent event; event.type = static_cast<RuntimeEventType>(input[6]);
    event.notification = static_cast<RuntimeNotificationType>(input[7]);
    HeadEventResult result = operation == 0 ? head.runtime_event(event) : head.button_event(input[8] != 0);
    state[0] = static_cast<unsigned>(head.state.notification);
    state[1] = head.state.encoder_press_hijacked;
    state[2] = static_cast<unsigned>(events.size());
    for (unsigned i = 0; i < events.size(); ++i) output[i] = events[i];
    return static_cast<int>(result);
}
