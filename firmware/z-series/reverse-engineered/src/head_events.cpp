#include "zseries/head_events.hpp"

namespace zseries { namespace oem {

HeadEventResult HeadEvents::effect(HeadEventEffect action, std::uint32_t argument) {
    if (!services.perform) return HeadEventResult::MissingService;
    return services.perform(services.context, action, argument)
        ? HeadEventResult::Ok : HeadEventResult::ServiceFailure;
}

HeadEventResult HeadEvents::runtime_event(const HeadRuntimeEvent& event) {
    if (event.type == RuntimeEventType::Transition) return HeadEventResult::Ok;
    if (event.type == RuntimeEventType::Notification) {
        HeadEventResult result = effect(HeadEventEffect::Alarm, 1);
        if (result != HeadEventResult::Ok) return result;
        state.notification = event.notification;
        return effect(HeadEventEffect::NotificationScreen,
                      static_cast<std::uint32_t>(event.notification));
    }
    // The original diagnoses an unknown event and produces no UI action.
    if (event.type != RuntimeEventType::Update) return HeadEventResult::Ok;
    bool alarm = state.instruction == OpCode::WaitT2Safety ||
                 state.instruction == OpCode::WaitPumpOk;
    if (!alarm && state.runtime == RuntimeStateId::Running && state.instruction == OpCode::WaitUser) {
        alarm = state.pause != PauseReason::User;
        if (state.pause == PauseReason::Program) {
            // Original two bit masks select these seven completion screens.
            switch (state.screen) {
                case ScreenId::None:
                case ScreenId::BrewCompleted:
                case ScreenId::PakBrewCompleted:
                case ScreenId::ChillCompleted:
                case ScreenId::PicoStillBoilPointFound:
                case ScreenId::CoffeeCompleted1:
                case ScreenId::StepCompleted: alarm = true; break;
                default: alarm = false; break;
            }
        }
    }
    const HeadEventResult result = effect(HeadEventEffect::Alarm, alarm);
    if (result != HeadEventResult::Ok) return result;
    return effect(HeadEventEffect::RunningScreen);
}

HeadEventResult HeadEvents::button_event(bool long_press) {
    if (long_press) return HeadEventResult::Ok;
    if (!state.encoder_press_hijacked) return effect(HeadEventEffect::ScreenButton);
    state.encoder_press_hijacked = false;
    return effect(HeadEventEffect::Synchronize);
}

}} // namespace zseries::oem
