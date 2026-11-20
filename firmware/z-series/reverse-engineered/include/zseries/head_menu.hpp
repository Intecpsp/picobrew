// Owned menu entries and concrete Z119 menu/title/body rendering.
#pragma once
#include "zseries/head_widgets.hpp"
#include "zseries/head_encoder.hpp"
#include "zseries/head_ui_commands.hpp"
#include <array>

namespace zseries { namespace oem {
struct HeadMenuItem {
    std::array<char,129> text{};
    HeadUiEvent event;
};
struct HeadMenuData {
    std::array<HeadMenuItem,32> items;
    std::uint32_t count = 0;
    LanguageType language = LanguageType::English;
    void clear() noexcept;
    bool append(const HeadUiEvent&,const char* label);
    bool append(UiCommand,const char* label = nullptr);
    bool prepend(UiCommand,const char* label = nullptr);
};
struct HeadMenuBody {
    const char* const* lines = nullptr;
    std::uint8_t count = 0;
    DisplayTextAlignment horizontal = DisplayTextAlignment::Center;
    std::uint32_t vertical = 1;
};
struct HeadMenuMarquee {
    std::array<std::uint8_t,3840> pixels{};
    DisplayPoint point{0,0};
    std::uint32_t width = 0, full_width = 0, source_x = 0;
    std::uint32_t pause_ms = 800, step_ms = 25, deadline = 0;
    bool active = false;
};
struct HeadMenuState {
    DisplayRect rect{0,0,0,0};
    std::uint32_t mode = 0, characters = 0, last_visible = 0;
    std::uint32_t gradient_steps = 0;
    bool gradient = true;
    HeadMenuMarquee marquee;
};
struct HeadMenuServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
};
enum class HeadMenuResult {
    Ok, MissingService, AssertionFailure, InvalidState, InvalidText,
    RectangleFailure, BitmapFailure, WidgetFailure
};
class HeadMenuRenderer {
public:
    HeadMenuRenderer(HeadWidgetPainter& painter,HeadMenuData& data,EncoderMenu& selection) noexcept
        : painter(painter),data(data),selection(selection) {}
    HeadWidgetPainter& painter;
    HeadMenuData& data;
    // Shares the exact selection/offset storage used by HeadEncoder.
    EncoderMenu& selection;
    HeadMenuState state;
    HeadMenuServices services;
    // 0x400f86c8: title, one/three/five-row menu and optional body composition.
    HeadMenuResult compose(DisplayRect& available,const char* title,std::uint32_t maximum_rows,
        const HeadMenuBody* body = nullptr,std::uint32_t vertical = 2,
        UiCommand default_command = UiCommand::None);
    HeadMenuResult initialize(const DisplayRect&,std::uint8_t rows); // 0x400eb14c
    HeadMenuResult draw(); // 0x400eb07c / 0x400e9750 / 0x400eaf5c
    HeadMenuResult tick(std::uint32_t now); // 0x400e79f8
private:
    HeadMenuResult assertion(std::uint32_t);
    HeadMenuResult rectangle(const DisplayRect&,std::uint8_t);
    HeadMenuResult rounded_selection(const DisplayRect&);
    HeadMenuResult item(std::uint32_t index,std::int32_t row,bool selected);
    HeadMenuResult draw_text(const char*,std::int32_t row,bool invert);
    HeadMenuResult prepare_marquee(const char*);
    HeadMenuResult draw_marquee();
};
}} // namespace zseries::oem
