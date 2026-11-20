// Original Head encoder dispatch and inline widget selection rules.
#pragma once
#include "zseries/head_flow.hpp"

namespace zseries { namespace oem {

struct EncoderMenu {
    bool enabled = false;
    std::uint32_t selected = 0, count = 0, visible_rows = 0;
    std::uint32_t selection_row = 0, scroll_offset = 0;
};
struct EncoderSlideshow {
    bool enabled = false;
    std::uint32_t selected = 0, count = 0;
    HeadTimer timer;
};
struct EncoderParagraph {
    bool enabled = false;
    std::int32_t offset = 0, visible_lines = 0, total_lines = 0;
};
struct EncoderCharacter {
    bool enabled = false;
    std::int32_t selected = 0, count = 0;
};
struct EncoderTemperature {
    bool enabled = false;
    std::int32_t value = 0, minimum = 0, maximum = 0;
};
struct EncoderDuration { bool enabled = false; std::uint32_t seconds = 0; };
struct EncoderStrength { bool enabled = false; std::int32_t value = 5; };
struct EncoderQuantity {
    bool enabled = false, wrap = false;
    std::int32_t selected = 0, count = 0;
};
struct EncoderCombined {
    bool enabled = false;
    std::uint8_t selected = 0, paragraph_count = 0, menu_count = 0;
};
enum class HeadEncoderEffect : std::uint8_t {
    MenuDraw, SlideshowSelect, ParagraphDraw, CharacterDraw, TemperatureDraw,
    DurationDraw, StrengthDraw, QuantityChanged, DiagnosticParagraph,
    CombinedParagraphSelect, CombinedMenuSelect
};
struct HeadEncoderServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    // SlideshowSelect and Combined*Select must apply their complete child
    // operation, including its selection state and presentation. They are
    // explicit boundaries here, not successful default drawing stubs.
    bool (*perform)(void*, HeadEncoderEffect, std::uint32_t, std::uint32_t) = nullptr;
};
struct HeadEncoderState {
    EncoderMenu menu;
    EncoderSlideshow slideshow;
    EncoderParagraph paragraph;
    EncoderCharacter character;
    EncoderTemperature temperature;
    EncoderDuration duration;
    EncoderStrength strength;
    EncoderQuantity quantities[2];
    EncoderCombined combined;
    std::uint8_t diagnostic_line = 0;
    HeadTimer transient_timer;
    bool display_dirty = false;
    std::uint32_t assertion_line = 0;
};
enum class HeadEncoderResult { Ok, MissingService, ServiceFailure, InvalidDelta, InvalidState, InvalidSelection };
class HeadEncoder {
public:
    HeadEncoderState state;
    HeadEncoderServices services;
    // 0x400fdd0c: payload after consumption; original input direction is reversed.
    HeadEncoderResult rotate(std::int32_t input_delta);
private:
    HeadEncoderResult effect(HeadEncoderEffect, std::uint32_t = 0, std::uint32_t = 0);
    HeadEncoderResult reset(HeadTimer&);
};

}} // namespace zseries::oem
