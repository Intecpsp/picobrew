#include "zseries/head_encoder.hpp"

namespace zseries { namespace oem {
namespace {
// Xtensa integer operations wrap at 32 bits. Avoid signed C++ overflow.
std::int32_t signed_word(std::uint32_t value) {
    return value <= 0x7fffffffu ? static_cast<std::int32_t>(value)
        : static_cast<std::int32_t>(static_cast<std::int64_t>(value) - 0x100000000ll);
}
std::int32_t plus(std::int32_t value, std::uint32_t delta) {
    return signed_word(static_cast<std::uint32_t>(value) + delta);
}
}
HeadEncoderResult HeadEncoder::effect(HeadEncoderEffect type, std::uint32_t first, std::uint32_t second) {
    if (!services.perform) return HeadEncoderResult::MissingService;
    return services.perform(services.context, type, first, second)
        ? HeadEncoderResult::Ok : HeadEncoderResult::ServiceFailure;
}
HeadEncoderResult HeadEncoder::reset(HeadTimer& timer) {
    if (!timer.active) return HeadEncoderResult::Ok;
    if (!services.millis) return HeadEncoderResult::MissingService;
    head_timer_reset(timer, services.millis(services.context));
    return HeadEncoderResult::Ok;
}
HeadEncoderResult HeadEncoder::rotate(std::int32_t input_delta) {
    const std::uint32_t delta = 0u - static_cast<std::uint32_t>(input_delta);
    if (!delta) { state.assertion_line = 0x2c2; return HeadEncoderResult::InvalidDelta; }
    HeadEncoderResult result;
    if (state.combined.enabled) {
        auto& combined = state.combined;
        const std::uint32_t count = static_cast<std::uint8_t>(combined.paragraph_count + combined.menu_count);
        const std::int32_t candidate = plus(combined.selected, delta);
        std::uint32_t next = candidate < 0 ? 0u : static_cast<std::uint32_t>(candidate);
        if (static_cast<std::int32_t>(count) <= candidate) next = count - 1u;
        if (combined.selected != next) {
            combined.selected = static_cast<std::uint8_t>(next);
            if (combined.selected < combined.paragraph_count) {
                result = effect(HeadEncoderEffect::CombinedParagraphSelect, combined.selected);
                if (result != HeadEncoderResult::Ok) return result;
            } else {
                if (combined.selected == combined.paragraph_count) {
                    result = effect(HeadEncoderEffect::CombinedParagraphSelect, combined.paragraph_count);
                    if (result != HeadEncoderResult::Ok) return result;
                }
                result = effect(HeadEncoderEffect::CombinedMenuSelect,
                    static_cast<std::uint8_t>(combined.selected - combined.paragraph_count));
                if (result != HeadEncoderResult::Ok) return result;
            }
        }
    } else {
        auto& menu = state.menu;
        if (menu.enabled) {
            std::uint32_t next = menu.selected + delta;
            if (signed_word(next) < 1) next = 0;
            else if (menu.count <= next) next = menu.count - 1u;
            if (menu.selected != next && menu.visible_rows) {
                menu.selected = next; menu.scroll_offset = next - menu.selection_row;
                result = effect(HeadEncoderEffect::MenuDraw);
                if (result != HeadEncoderResult::Ok) return result;
                state.display_dirty = true;
            }
        }
        auto& slideshow = state.slideshow;
        if (slideshow.enabled) {
            if (!slideshow.count) return HeadEncoderResult::InvalidState;
            const std::uint32_t next = (slideshow.count + slideshow.selected + delta) % slideshow.count;
            if (next != slideshow.selected) {
                result = effect(HeadEncoderEffect::SlideshowSelect, next);
                if (result != HeadEncoderResult::Ok) return result;
            }
            slideshow.timer.duration = 60000;
            result = reset(slideshow.timer); if (result != HeadEncoderResult::Ok) return result;
        }
        auto& paragraph = state.paragraph;
        if (paragraph.enabled && paragraph.visible_lines) {
            std::int32_t next = plus(paragraph.offset, delta);
            const std::int32_t maximum = plus(paragraph.total_lines, 0u - static_cast<std::uint32_t>(paragraph.visible_lines));
            if (next >= maximum) next = maximum;
            if (next < 0) next = 0;
            if (paragraph.offset != next) {
                paragraph.offset = next;
                result = effect(HeadEncoderEffect::ParagraphDraw);
                if (result != HeadEncoderResult::Ok) return result;
            }
        }
        auto& character = state.character;
        if (character.enabled) {
            std::int32_t next = plus(character.selected, delta);
            if (next < 0) {
                next = plus(next, static_cast<std::uint32_t>(character.count));
                if (next < 0) { state.assertion_line = 0x481; return HeadEncoderResult::InvalidSelection; }
            }
            if (character.count <= next) next = plus(next, 0u - static_cast<std::uint32_t>(character.count));
            character.selected = next;
            result = effect(HeadEncoderEffect::CharacterDraw);
            if (result != HeadEncoderResult::Ok) return result;
            state.display_dirty = true;
        }
        auto& temperature = state.temperature;
        if (temperature.enabled) {
            std::int32_t next = plus(temperature.value, delta);
            if (next < temperature.minimum) next = temperature.minimum;
            if (temperature.maximum < next) next = temperature.maximum;
            temperature.value = next;
            result = effect(HeadEncoderEffect::TemperatureDraw);
            if (result != HeadEncoderResult::Ok) return result;
            state.display_dirty = true;
        }
        if (state.duration.enabled) {
            std::uint32_t& seconds = state.duration.seconds;
            const std::uint32_t increment = seconds < 3600 ? 60 : seconds < 10800 ? 300 : seconds < 43200 ? 900 : 3600;
            seconds -= seconds % increment;
            const std::int32_t next = signed_word(seconds + increment * delta);
            seconds = next < 0 ? 0u : next > 259199 ? 259200u : static_cast<std::uint32_t>(next);
            result = effect(HeadEncoderEffect::DurationDraw);
            if (result != HeadEncoderResult::Ok) return result;
        }
        if (state.strength.enabled) {
            const std::int32_t next = plus(state.strength.value, delta);
            state.strength.value = next < 1 ? 1 : next < 10 ? next : 10;
            result = effect(HeadEncoderEffect::StrengthDraw);
            if (result != HeadEncoderResult::Ok) return result;
            state.display_dirty = true;
        }
        for (std::uint32_t index = 0; index != 2; ++index) {
            auto& quantity = state.quantities[index];
            if (!quantity.enabled) continue;
            std::int32_t next = plus(quantity.selected, delta);
            const std::int32_t last = plus(quantity.count, 0xffffffffu);
            if (next < 0) next = quantity.wrap ? last : 0;
            if (quantity.count <= next) next = quantity.wrap ? 0 : last;
            if (quantity.selected != next) {
                quantity.selected = next;
                result = effect(HeadEncoderEffect::QuantityChanged, index, static_cast<std::uint32_t>(next));
                if (result != HeadEncoderResult::Ok) return result;
            }
        }
        if (state.diagnostic_line) {
            result = effect(HeadEncoderEffect::DiagnosticParagraph, delta);
            if (result != HeadEncoderResult::Ok) return result;
        }
    }
    return reset(state.transient_timer);
}
}} // namespace zseries::oem
