#pragma once
#include "zseries/display_fonts.hpp"

namespace zseries { namespace oem {
constexpr std::uint32_t DisplayInvalidCodepoint=0x10f801;
enum class DisplayTextResult {
    Ok, InvalidFont, InvalidRegion, MissingText, UnterminatedText,
    TextTooLong, EmptyText, TruncatedUtf8, OverlongUtf8,
    InvalidPosition, BitmapFailure
};
struct DisplayUtf8 {
    const std::uint8_t* bytes=nullptr;
    std::uint16_t cursor=0, length=0, characters=0;
};
DisplayTextResult display_utf8_initialize(DisplayUtf8&,const char*,std::size_t capacity) noexcept;
DisplayTextResult display_utf8_next(DisplayUtf8&,std::uint32_t& codepoint) noexcept;
DisplayTextResult display_utf8_length(const char*,std::size_t capacity,std::uint32_t& count) noexcept;

struct DisplayTextContext {
    const DisplayFont* font=nullptr;
    std::uint32_t line_spacing=0;
    DisplayRect bounds=DisplayBounds;
    std::uint32_t width=128, height=64, cell_width=0, line_height=0, rows=0, columns=0;
};
DisplayTextResult display_text_font(DisplayTextContext&,const DisplayFont&,std::uint32_t line_spacing=0) noexcept;
DisplayTextResult display_text_bounds(DisplayTextContext&,const DisplayRect&) noexcept;

enum class DisplayTextAlignment : std::uint32_t { Left=0, Center=1, Right=2 };
struct DisplayTextOptions {
    std::uint32_t characters=0;
    std::int32_t row=0;
    bool invert=false;
    DisplayTextAlignment alignment=DisplayTextAlignment::Left;
    std::int32_t offset_x=0,offset_y=0;
};
DisplayTextResult display_glyphs(DisplayState&,const DisplayFont&,const char*,std::size_t capacity,
                               std::uint32_t characters,DisplayPoint,bool invert=false) noexcept;
DisplayTextResult display_text(DisplayState&,const DisplayTextContext&,const char*,std::size_t capacity,
                              const DisplayTextOptions&) noexcept;
}} // namespace zseries::oem
