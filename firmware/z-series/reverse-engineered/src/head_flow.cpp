#include "zseries/head_flow.hpp"
#include <cstring>

namespace zseries { namespace oem {

void head_timer_reset(HeadTimer& timer, std::uint32_t now) noexcept {
    if (timer.active) timer.deadline = timer.duration + now;
}
bool head_timer_start(HeadTimer& timer, std::uint32_t duration, std::uint32_t now) noexcept {
    if (!duration) return false;
    timer.active = true; timer.duration = duration; head_timer_reset(timer, now); return true;
}
HeadFlowResult HeadFlow::effect(HeadEffect type, std::uint32_t argument,
                                 std::uint32_t second, std::uint32_t third) {
    if (!services.perform) return HeadFlowResult::MissingService;
    HeadAction action(type); action.argument = argument; action.second = second; action.third = third;
    return services.perform(services.context, action) ? HeadFlowResult::Ok : HeadFlowResult::ServiceFailure;
}
HeadFlowResult HeadFlow::synchronize() {
    const auto id = static_cast<std::uint32_t>(observed.core);
    if (id <= 1) return HeadFlowResult::Ok;
    if (observed.core == CoreStateId::Running || observed.core == CoreStateId::Stopping)
        return effect(HeadEffect::RunningScreen);
    if (observed.core == CoreStateId::Registering) return effect(HeadEffect::Message, 0x24);
    if (observed.core == CoreStateId::NeedsNetwork) return effect(HeadEffect::NetworkRequired);
    return effect(HeadEffect::HomeScreen);
}
HeadFlowResult HeadFlow::dispatch(CoreCommandId command) {
    const std::uint32_t id = static_cast<std::uint32_t>(command);
    HeadFlowResult result = effect(HeadEffect::PostCoreCommand, id);
    if (result != HeadFlowResult::Ok) return result;
    if (id >= 14 && id <= 16) return effect(HeadEffect::Message, 0x21);
    // Original suppression mask: recipe-list requests and Runtime control
    // commands keep their current screen. WiFi scan and standby do as well.
    if (id != 41 && id != 49 && (id > 30 || !(0x7e000c08u & (1u << id)))) return synchronize();
    return HeadFlowResult::Ok;
}
HeadFlowResult HeadFlow::alarm(bool enabled) {
    if (enabled && !alarm_duration_ms) return HeadFlowResult::InvalidDuration;
    if (enabled && !services.millis) return HeadFlowResult::MissingService;
    HeadFlowResult result = effect(HeadEffect::SystemAlarm, enabled);
    if (result != HeadFlowResult::Ok) return result;
    if (!enabled) { alarm_timer.active = false; alarm_timer.deadline = 0; }
    else head_timer_start(alarm_timer, alarm_duration_ms, services.millis(services.context));
    return HeadFlowResult::Ok;
}
HeadFlowResult HeadFlow::start_transient(std::uint32_t duration) {
    if (!duration) return HeadFlowResult::InvalidDuration;
    if (!services.millis) return HeadFlowResult::MissingService;
    head_timer_start(transient_timer, duration, services.millis(services.context)); return HeadFlowResult::Ok;
}
HeadFlowResult HeadFlow::reset_transient() {
    if (!transient_timer.active) return HeadFlowResult::Ok;
    if (!services.millis) return HeadFlowResult::MissingService;
    head_timer_reset(transient_timer, services.millis(services.context)); return HeadFlowResult::Ok;
}
HeadFlowResult HeadFlow::tick(std::uint32_t now) {
    if (!services.millis || !services.pending || !services.handle_event || !services.perform)
        return HeadFlowResult::MissingService;
    const HeadEventKind priority[] = {HeadEventKind::Core, HeadEventKind::Button, HeadEventKind::Encoder,
        HeadEventKind::TextInput, HeadEventKind::Temperature, HeadEventKind::Time, HeadEventKind::Abv,
        HeadEventKind::Valve, HeadEventKind::Quantity, HeadEventKind::Still, HeadEventKind::Wifi,
        HeadEventKind::Runtime, HeadEventKind::UiCommand, HeadEventKind::Azure};
    bool handled = false;
    for (HeadEventKind event : priority) {
        if (!services.pending(services.context, event)) continue;
        if (!services.handle_event(services.context, event)) return HeadFlowResult::ServiceFailure;
        handled = true;
        if (event == HeadEventKind::Still && observed.runtime == RuntimeStateId::Running && observed.instruction == OpCode::WaitStillReady) {
            const HeadFlowResult result = effect(HeadEffect::StillScreen);
            if (result != HeadFlowResult::Ok) return result;
        }
        break;
    }
    if (!handled && observed.downloading) {
        std::int32_t percentage = 0;
        if (observed.download_bytes > 0) {
            const std::uint32_t bits = static_cast<std::uint32_t>(observed.downloaded_bytes) * 100u;
            std::int32_t numerator; std::memcpy(&numerator, &bits, sizeof numerator);
            percentage = numerator / observed.download_bytes;
        }
        const HeadFlowResult result = effect(HeadEffect::DownloadProgress, static_cast<std::uint32_t>(percentage));
        if (result != HeadFlowResult::Ok) return result;
    }
    if (transient_timer.active && transient_timer.deadline <= now) {
        transient_timer.active = false; transient_timer.deadline = 0;
        const HeadFlowResult result = synchronize(); if (result != HeadFlowResult::Ok) return result;
    }
    if (alarm_timer.active && alarm_timer.deadline <= now) {
        // The OEM samples the clock for its alarm-timeout diagnostic first.
        services.millis(services.context);
        HeadFlowResult result = alarm(false); if (result != HeadFlowResult::Ok) return result;
        result = dispatch(CoreCommandId::Stop); if (result != HeadFlowResult::Ok) return result;
    }
    const std::uint32_t render_time = services.millis(services.context);
    HeadFlowResult result = effect(HeadEffect::ScreensTick, render_time);
    if (result != HeadFlowResult::Ok) return result;
    return effect(HeadEffect::DisplayTick, render_time);
}
}} // namespace zseries::oem
