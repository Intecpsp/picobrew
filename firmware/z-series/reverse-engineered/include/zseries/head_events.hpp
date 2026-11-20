// Z119 Head runtime-notification and encoder-button event bodies.
#pragma once
#include "zseries/enums.hpp"
#include <cstdint>

namespace zseries { namespace oem {

struct HeadRuntimeEvent {
    RuntimeEventType type = RuntimeEventType::Transition;
    RuntimeNotificationType notification = RuntimeNotificationType::None;
};
struct HeadEventState {
    RuntimeStateId runtime = RuntimeStateId::Off;
    OpCode instruction = OpCode::Noop;
    PauseReason pause = PauseReason::None;
    ScreenId screen = ScreenId::None;
    RuntimeNotificationType notification = RuntimeNotificationType::None;
    bool encoder_press_hijacked = false;
};
enum class HeadEventEffect : std::uint8_t {
    Alarm, RunningScreen, NotificationScreen, ScreenButton, Synchronize
};
struct HeadEventServices {
    void* context = nullptr;
    // Alarm is the recovered HeadFlow::alarm operation, including its timer.
    // All presentation actions require the real ScreenLibrary implementation.
    bool (*perform)(void*, HeadEventEffect, std::uint32_t argument) = nullptr;
};
enum class HeadEventResult { Ok, MissingService, ServiceFailure };

class HeadEvents {
public:
    HeadEventState state;
    HeadEventServices services;
    // Called after consuming the selected one-slot event, as in HeadFlow.
    // 0x40107984: Transition is silent; Update and Notification route effects.
    HeadEventResult runtime_event(const HeadRuntimeEvent&);
    // 0x4010718c: long presses are discarded; short presses may be hijacked.
    HeadEventResult button_event(bool long_press);
private:
    HeadEventResult effect(HeadEventEffect, std::uint32_t argument = 0);
};

}} // namespace zseries::oem
