// Z119 gradients, caret drawing and timed bitmap animation.
#pragma once
#include "zseries/display.hpp"

namespace zseries { namespace oem {

enum class DisplayEffectResult {
    Ok, InvalidRectangle, InvalidHeight, InvalidAnimation, MissingIo,
    BitmapFailure, InvalidMotion, MissingDelay
};

// These operations darken existing pixels; they do not paint an opaque ramp.
// The booleans preserve the original function arguments, including their
// unsigned 8-bit ramp arithmetic.
DisplayEffectResult display_vertical_gradient(DisplayState&, const DisplayRect&, bool ascending) noexcept;
DisplayEffectResult display_horizontal_gradient(DisplayState&, const DisplayRect&,
                                                  bool white_origin, bool descending) noexcept;
DisplayEffectResult display_caret(DisplayState&, DisplayPoint, std::int32_t height,
                                  std::uint8_t color, std::uint32_t direction) noexcept;

struct DisplayAnimation {
    std::uint32_t interval_ms, repeats, frame_count;
    const DisplayBitmap* const* frames;
};
struct DisplayAnimationState {
    const DisplayAnimation* animation = nullptr;
    std::uint32_t cycle = 0, frame = 0, deadline = 0;
    DisplayPoint point{0, 0};
    bool dirty = false;
};
DisplayEffectResult display_animation_tick(DisplayState&, DisplayAnimationState&,
                                            const DisplayIo&, std::uint32_t now) noexcept;

struct DisplayDelay {
    void* context = nullptr;
    void (*milliseconds)(void*, std::uint32_t) = nullptr;
};
// Direction 0/1 enters from the left/right; 2/3 enters from the top/bottom.
// The original draws each position before advancing and excludes the final
// target. Unreachable targets or nonpositive steps are explicit errors here.
DisplayEffectResult display_slide(DisplayState&, const DisplayIo&, const DisplayDelay&,
                                   const DisplayBitmap&, std::uint32_t direction,
                                   DisplayPoint target, std::int32_t step,
                                   std::uint32_t delay_ms) noexcept;
DisplayEffectResult display_slide_aligned(DisplayState&, const DisplayIo&, const DisplayDelay&,
                                           const DisplayBitmap&, std::uint32_t direction,
                                           std::uint32_t horizontal, std::uint32_t vertical,
                                           std::int32_t step, std::uint32_t delay_ms) noexcept;

}} // namespace zseries::oem
