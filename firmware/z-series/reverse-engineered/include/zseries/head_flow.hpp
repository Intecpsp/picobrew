// Z119 front-panel event priority, UI synchronization and alarm timer routing.
#pragma once
#include "zseries/enums.hpp"
#include <cstdint>

namespace zseries { namespace oem {

enum class HeadEventKind : std::uint8_t {
    Core, Button, Encoder, TextInput, Temperature, Time, Abv, Valve, Quantity,
    Still, Wifi, Runtime, UiCommand, Azure
};
struct HeadTimer {
    bool active = false;
    std::uint32_t duration = 0, deadline = 0;
};
void head_timer_reset(HeadTimer&, std::uint32_t now) noexcept;
bool head_timer_start(HeadTimer&, std::uint32_t duration, std::uint32_t now) noexcept;

enum class HeadEffect : std::uint8_t {
    RunningScreen, Message, NetworkRequired, HomeScreen, SystemAlarm,
    PostCoreCommand, StillScreen, DownloadProgress, ScreensTick, DisplayTick
};
struct HeadAction {
    HeadEffect effect;
    std::uint32_t argument = 0, second = 0, third = 0;
    explicit HeadAction(HeadEffect value) noexcept : effect(value) {}
};
struct HeadFlowServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    bool (*pending)(void*, HeadEventKind) = nullptr;
    // The selected handler consumes its original one-slot mailbox and applies
    // its recovered event-specific behavior. No default handlers are supplied.
    bool (*handle_event)(void*, HeadEventKind) = nullptr;
    // PostCoreCommand reports that the queue operation was serviced, including
    // its original drop-new behavior if the Core mailbox is already occupied.
    bool (*perform)(void*, const HeadAction&) = nullptr;
};
struct HeadFlowObserved {
    CoreStateId core = CoreStateId::Off;
    RuntimeStateId runtime = RuntimeStateId::Off;
    OpCode instruction = OpCode::Noop;
    bool downloading = false;
    std::int32_t downloaded_bytes = 0, download_bytes = 0;
};
enum class HeadFlowResult { Ok, MissingService, ServiceFailure, InvalidDuration };

class HeadFlow {
public:
    HeadFlowServices services;
    HeadFlowObserved observed;
    HeadTimer transient_timer, alarm_timer;
    std::uint32_t transient_duration_ms = 7000;
    std::uint32_t secondary_duration_ms = 30000;
    std::uint32_t alarm_duration_ms = 7200000;

    // 0x40109958: one event per tick, followed by timers and screen/display ticks.
    HeadFlowResult tick(std::uint32_t now);
    // 0x401070e8 / 0x401074f8 / 0x400deb58.
    HeadFlowResult synchronize();
    HeadFlowResult dispatch(CoreCommandId);
    HeadFlowResult alarm(bool enabled);
    HeadFlowResult start_transient(std::uint32_t duration);
    HeadFlowResult reset_transient();
private:
    HeadFlowResult effect(HeadEffect, std::uint32_t argument = 0,
                          std::uint32_t second = 0, std::uint32_t third = 0);
};

}} // namespace zseries::oem
