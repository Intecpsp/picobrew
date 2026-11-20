// Original scrolling paragraph, footer button and blinking carets.
#pragma once
#include "zseries/head_widgets.hpp"
#include "zseries/head_encoder.hpp"
#include <array>

namespace zseries { namespace oem {
struct HeadScrollData {
    std::array<std::array<char,65>,32> lines{};
    std::uint32_t count = 0;
    void clear() noexcept {count=0;for(auto& line:lines)line.fill(0);}
    bool append(const char*);
};
struct HeadParagraphState {
    DisplayRect rect{0,0,0,0}, up{0,0,0,0}, down{0,0,0,0};
    DisplayTextAlignment alignment = DisplayTextAlignment::Center;
    UiCommand command = UiCommand::None;
    std::array<char,129> button{};
    std::uint32_t text_offset = 0, deadline = 0;
    std::uint16_t interval_ms = 125;
    bool caret_visible = false;
};
struct HeadParagraphServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
};
enum class HeadParagraphResult {Ok,MissingService,InvalidState,InvalidText,RectangleFailure,BitmapFailure};
class HeadParagraphRenderer {
public:
    HeadParagraphRenderer(HeadWidgetPainter& painter,HeadScrollData& data,EncoderParagraph& selection) noexcept
        : painter(painter),data(data),selection(selection) {}
    HeadWidgetPainter& painter;
    HeadScrollData& data;
    EncoderParagraph& selection;
    HeadParagraphState state;
    HeadParagraphServices services;
    HeadParagraphResult initialize(const DisplayRect&,DisplayTextAlignment,UiCommand,
        const char* button,bool reserve_bottom); // 0x400e9a2c
    HeadParagraphResult draw(); // 0x400e9814
    HeadParagraphResult caret(); // 0x400e95bc
private:
    HeadParagraphResult text(const DisplayRect&,const char*,std::uint32_t,bool invert,DisplayTextAlignment);
};
}} // namespace zseries::oem
