#include "zseries/display_effects.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
namespace {
std::int32_t signed_bits(std::uint32_t value) noexcept {
    std::int32_t result; std::memcpy(&result, &value, sizeof result); return result;
}
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return signed_bits(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
void darken(DisplayState& state, std::int32_t x, std::int32_t y, std::uint8_t limit) noexcept {
    std::uint8_t& pair = state.pixels[static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x) / 2];
    if (x & 1) pair = static_cast<std::uint8_t>((pair & 0xf0) | std::min<std::uint8_t>(pair & 15, limit));
    else pair = static_cast<std::uint8_t>((pair & 15) | (std::min<std::uint8_t>(pair >> 4, limit) << 4));
}
struct Span {
    std::uint32_t leading, pairs, trailing;
    explicit Span(const DisplayRect& r) noexcept {
        const std::uint32_t width = display_rect_width(r);
        leading = r.left & 1;
        pairs = (width - leading) / 2;
        // The zero trailing-byte offset is also the OEM no-tail sentinel.
        // An even-X one-pixel span consequently touches no pixels.
        trailing = leading != (width & 1) && width / 2 != 0 ? 1 : 0;
    }
    std::uint32_t count() const noexcept { return leading + pairs * 2 + trailing; }
};
bool complete_io(const DisplayIo& io) noexcept { return io.digital_write && io.spi_byte && io.spi_bytes; }
DisplayEffectResult bitmap_result(DisplayResult result) noexcept {
    return result == DisplayResult::Ok ? DisplayEffectResult::Ok : DisplayEffectResult::BitmapFailure;
}
} // namespace

DisplayEffectResult display_vertical_gradient(DisplayState& state, const DisplayRect& region, bool ascending) noexcept {
    if (!display_rect_intersects(DisplayBounds, region)) return DisplayEffectResult::Ok;
    DisplayRect clipped;
    if (!display_rect_intersection(DisplayBounds, region, clipped)) return DisplayEffectResult::InvalidRectangle;
    const Span span(clipped);
    const std::uint32_t height = display_rect_height(clipped), increment = 16 / (height + 1);
    std::uint8_t level = static_cast<std::uint8_t>(ascending ? 15 - (height + 1) * increment : 15);
    for (std::int32_t y = clipped.bottom; y >= clipped.top; --y) {
        level = static_cast<std::uint8_t>(ascending ? level + increment : level - increment);
        for (std::uint32_t x = 0; x < span.count(); ++x) darken(state, clipped.left + x, y, level);
    }
    return DisplayEffectResult::Ok;
}

DisplayEffectResult display_horizontal_gradient(DisplayState& state, const DisplayRect& region,
                                                  bool white_origin, bool descending) noexcept {
    if (!display_rect_intersects(DisplayBounds, region)) return DisplayEffectResult::Ok;
    DisplayRect clipped;
    if (!display_rect_intersection(DisplayBounds, region, clipped)) return DisplayEffectResult::InvalidRectangle;
    const Span span(clipped);
    const std::uint32_t count = span.count(), increment = 16 / (count + 1), origin = white_origin ? 15 : 0;
    for (std::int32_t y = clipped.bottom; y >= clipped.top; --y) {
        std::uint8_t level = static_cast<std::uint8_t>(origin - (descending ? increment : count * increment));
        for (std::uint32_t x = 0; x < count; ++x) {
            darken(state, clipped.left + x, y, level);
            level = static_cast<std::uint8_t>(descending ? level - increment : level + increment);
        }
    }
    return DisplayEffectResult::Ok;
}

DisplayEffectResult display_caret(DisplayState& state, DisplayPoint point, std::int32_t height,
                                  std::uint8_t color, std::uint32_t direction) noexcept {
    if (height < 2 || height > 4096) return DisplayEffectResult::InvalidHeight;
    DisplayRect line{point.x, point.y, point.x, point.y};
    DisplayRect delta{1, 1, 1, 1};
    const std::int32_t span = height * 2 - 1;
    switch (direction) {
    case 0: line.bottom = add(line.top, span - 1); delta.bottom = -1; break;
    case 1: line.top = line.bottom = add(line.top, span / 2); delta.top = -1; break;
    case 2: line.right = add(line.left, span - 1); delta.right = -1; break;
    case 3: line.left = line.right = add(line.left, span / 2); delta.left = -1; break;
    default: break; // The OEM default paints a diagonal of single pixels.
    }
    for (std::int32_t row = 0; row < height; ++row) {
        const DisplayResult result = display_rectangle(state, line, color, true);
        if (result != DisplayResult::Ok) return DisplayEffectResult::InvalidRectangle;
        line.left = add(line.left, delta.left); line.top = add(line.top, delta.top);
        line.right = add(line.right, delta.right); line.bottom = add(line.bottom, delta.bottom);
    }
    return DisplayEffectResult::Ok;
}

DisplayEffectResult display_animation_tick(DisplayState& state, DisplayAnimationState& motion,
                                            const DisplayIo& io, std::uint32_t now) noexcept {
    const DisplayAnimation* animation = motion.animation;
    if (animation && motion.deadline < now) {
        if (!animation->frames || !animation->frame_count || motion.frame >= animation->frame_count)
            return DisplayEffectResult::InvalidAnimation;
        if (!complete_io(io)) return DisplayEffectResult::MissingIo;
        const std::uint32_t previous = motion.frame;
        motion.deadline = animation->interval_ms + now;
        ++motion.frame;
        if (motion.frame >= animation->frame_count) {
            ++motion.cycle;
            if (animation->repeats && motion.cycle >= animation->repeats) motion.animation = nullptr;
            else motion.frame = 0;
        }
        // Original tick advances first, then draws the previous index. The
        // final finite-repeat tick clears the animation without drawing it.
        if (motion.animation && animation->frames[previous]) {
            const DisplayBitmap& frame = *animation->frames[previous];
            const DisplayResult result = display_bitmap(state, motion.point, frame, frame.bounds);
            if (result != DisplayResult::Ok) return bitmap_result(result);
            display_flush(state, io);
        }
    }
    if (motion.dirty) {
        if (!complete_io(io)) return DisplayEffectResult::MissingIo;
        motion.dirty = false;
        display_flush(state, io);
    }
    return DisplayEffectResult::Ok;
}

DisplayEffectResult display_slide(DisplayState& state, const DisplayIo& io, const DisplayDelay& delay,
                                   const DisplayBitmap& bitmap, std::uint32_t direction,
                                   DisplayPoint target, std::int32_t step, std::uint32_t delay_ms) noexcept {
    if (direction > 3 || step <= 0 || bitmap.width > INT32_MAX || bitmap.height > INT32_MAX)
        return DisplayEffectResult::InvalidMotion;
    if (!complete_io(io)) return DisplayEffectResult::MissingIo;
    if (!delay.milliseconds) return DisplayEffectResult::MissingDelay;
    DisplayPoint position = target;
    const bool horizontal = direction < 2;
    if (direction == 0) position.x = -static_cast<std::int32_t>(bitmap.width);
    else if (direction == 1) position.x = DisplayWidth;
    else if (direction == 2) position.y = -static_cast<std::int32_t>(bitmap.height);
    else position.y = DisplayHeight;
    const std::int64_t distance = horizontal ? static_cast<std::int64_t>(target.x) - position.x
                                               : static_cast<std::int64_t>(target.y) - position.y;
    const std::int32_t increment = direction == 1 || direction == 3 ? -step : step;
    if (distance % increment || distance / increment < 0 || distance / increment > 4096)
        return DisplayEffectResult::InvalidMotion;
    for (std::int64_t frame = 0; frame < distance / increment; ++frame) {
        display_clear(state);
        const DisplayResult result = display_bitmap(state, position, bitmap, bitmap.bounds);
        if (result != DisplayResult::Ok) return bitmap_result(result);
        display_flush(state, io);
        delay.milliseconds(delay.context, delay_ms);
        if (horizontal) position.x = add(position.x, increment);
        else position.y = add(position.y, increment);
    }
    return DisplayEffectResult::Ok;
}

DisplayEffectResult display_slide_aligned(DisplayState& state, const DisplayIo& io, const DisplayDelay& delay,
                                           const DisplayBitmap& bitmap, std::uint32_t direction,
                                           std::uint32_t horizontal, std::uint32_t vertical,
                                           std::int32_t step, std::uint32_t delay_ms) noexcept {
    const std::uint32_t x = static_cast<std::uint32_t>(DisplayWidth) - bitmap.width;
    const std::uint32_t y = static_cast<std::uint32_t>(DisplayHeight) - bitmap.height;
    const DisplayPoint target{signed_bits(horizontal == 1 ? x >> 1 : horizontal == 2 ? x : 0),
                              signed_bits(vertical == 1 ? y >> 1 : vertical == 2 ? y : 0)};
    return display_slide(state, io, delay, bitmap, direction, target, step, delay_ms);
}
}} // namespace zseries::oem
