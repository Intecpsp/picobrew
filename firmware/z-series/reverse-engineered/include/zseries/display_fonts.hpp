// Packed font bitmaps extracted from the hash-verified Z119 OEM image.
#pragma once
#include "zseries/display.hpp"
namespace zseries { namespace oem {
struct DisplayFont {
    std::uint32_t glyph_width, glyph_height;
    DisplayBitmap bitmap;
};
enum class DisplayFontId { Font9x11, Font17x20, Font14x16, Font8x16, Font6x8 };
const DisplayFont* display_font(DisplayFontId) noexcept;
}} // namespace zseries::oem
