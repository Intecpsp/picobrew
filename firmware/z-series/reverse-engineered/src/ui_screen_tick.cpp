#include "zseries/ui_screen_tick.hpp"
namespace zseries { namespace oem {
UiScreenTickResult UiScreenTick::effect(UiScreenTickEffect type, std::uint32_t first, std::uint32_t second) {
    if (!services.perform) return UiScreenTickResult::MissingService;
    return services.perform(services.context,type,first,second)
        ? UiScreenTickResult::Ok : UiScreenTickResult::ServiceFailure;
}
UiScreenTickResult UiScreenTick::tick(std::uint32_t now) {
    UiScreenTickResult result;
    if (state.combined) {
        auto& caret=state.combined_caret;
        if (caret.enabled && caret.deadline <= now) {
            result=effect(UiScreenTickEffect::ScrollCaretDraw,1);
            if (result!=UiScreenTickResult::Ok) return result;
            caret.deadline=now+caret.interval_ms;
        }
        return effect(UiScreenTickEffect::MenuTextScrollTick,now,1);
    }
    if (state.runtime_state.enabled && state.runtime_state.deadline < now) {
        result=effect(UiScreenTickEffect::RuntimeStateDraw);
        if (result!=UiScreenTickResult::Ok) return result;
        state.runtime_state.deadline=now+500;
    }
    if (state.still_transient.enabled && state.still_transient.deadline < now) {
        result=effect(UiScreenTickEffect::StillTransientDraw);
        if (result!=UiScreenTickResult::Ok) return result;
        state.still_transient.deadline=now+500;
    }
    result=effect(UiScreenTickEffect::MenuTextScrollTick,now);
    if (result!=UiScreenTickResult::Ok) return result;
    result=effect(UiScreenTickEffect::SlideshowTick,now);
    if (result!=UiScreenTickResult::Ok) return result;
    if (state.pause.enabled && state.pause.deadline < now) {
        result=effect(UiScreenTickEffect::PauseDraw,now);
        if (result!=UiScreenTickResult::Ok) return result;
    }
    if (state.runtime_transient.enabled && state.runtime_transient.deadline < now) {
        result=effect(UiScreenTickEffect::RuntimeTransientDraw);
        if (result!=UiScreenTickResult::Ok) return result;
    }
    if (state.notification.enabled && state.notification.deadline < now) {
        result=effect(UiScreenTickEffect::NotificationDraw);
        if (result!=UiScreenTickResult::Ok) return result;
    }
    auto& waiting=state.waiting;
    if (waiting.enabled && waiting.animate && waiting.deadline < now) {
        waiting.deadline=now+500;
        if (!waiting.frame_count) return UiScreenTickResult::InvalidFrameCount;
        waiting.frame=static_cast<std::uint8_t>((static_cast<std::uint32_t>(waiting.frame)+1u)%waiting.frame_count);
        result=effect(UiScreenTickEffect::WaitingDraw);
        if (result!=UiScreenTickResult::Ok) return result;
    }
    if (state.level.enabled && state.level.deadline < now) {
        result=effect(UiScreenTickEffect::LevelDraw);
        if (result!=UiScreenTickResult::Ok) return result;
        state.level.deadline=now+250;
    }
    auto& caret=state.paragraph_caret;
    if (caret.enabled && caret.deadline <= now) {
        result=effect(UiScreenTickEffect::ScrollCaretDraw);
        if (result!=UiScreenTickResult::Ok) return result;
        caret.deadline=now+caret.interval_ms;
    }
    return UiScreenTickResult::Ok;
}
}} // namespace zseries::oem
