// Original ScreenLibrary periodic dispatch, with explicit drawing services.
#pragma once
#include <cstdint>
namespace zseries { namespace oem {
struct UiPeriodicState { bool enabled = false; std::uint32_t deadline = 0; };
struct UiCaretTimer : UiPeriodicState { std::uint16_t interval_ms = 0; };
struct UiWaitingTimer : UiPeriodicState {
    bool animate = false;
    std::uint8_t frame = 0, frame_count = 0;
};
enum class UiScreenTickEffect : std::uint8_t {
    RuntimeStateDraw, StillTransientDraw, MenuTextScrollTick, SlideshowTick,
    PauseDraw, RuntimeTransientDraw, NotificationDraw, WaitingDraw, LevelDraw,
    ScrollCaretDraw
};
struct UiScreenTickServices {
    void* context = nullptr;
    // first is the supplied tick time for tick/pause calls; for caret drawing
    // it identifies the combined (1) or normal (0) paragraph. second identifies
    // the combined (1) or normal (0) menu for MenuTextScrollTick.
    bool (*perform)(void*, UiScreenTickEffect, std::uint32_t first, std::uint32_t second) = nullptr;
};
struct UiScreenTickState {
    bool combined = false;
    UiCaretTimer combined_caret, paragraph_caret;
    UiPeriodicState runtime_state, still_transient, pause, runtime_transient, notification, level;
    UiWaitingTimer waiting;
};
enum class UiScreenTickResult { Ok, MissingService, ServiceFailure, InvalidFrameCount };
class UiScreenTick {
public:
    UiScreenTickState state;
    UiScreenTickServices services;
    // 0x40103e44, including the periodic gate at 0x400f181c.
    UiScreenTickResult tick(std::uint32_t now);
private:
    UiScreenTickResult effect(UiScreenTickEffect, std::uint32_t = 0, std::uint32_t = 0);
};
}} // namespace zseries::oem
