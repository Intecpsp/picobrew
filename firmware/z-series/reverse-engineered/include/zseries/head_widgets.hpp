// Concrete Z119 front-panel widget painting over the recovered framebuffer.
#pragma once
#include "zseries/display_text.hpp"
#include "zseries/ui_catalog.hpp"
#include <cstdint>

namespace zseries { namespace oem {
struct HeadWidgetServices {
    void* context = nullptr;
    bool (*assertion)(void*,std::uint32_t original_line) = nullptr;
};
enum class HeadWidgetResult { Ok, MissingService, AssertionFailure, InvalidRectangle, InvalidText, BitmapFailure };
class HeadWidgetPainter {
public:
    explicit HeadWidgetPainter(DisplayState& display) noexcept : display(display) {}
    DisplayState& display;
    LanguageType language = LanguageType::English;
    HeadWidgetServices services;
    bool dirty = false;
    std::uint32_t assertion_line = 0;
    // 0x400f8424; consumes 12 pixels, or 14 with the underline. The original
    // sixteen-byte title storage and thirteen-byte ellipsis prefix are retained.
    HeadWidgetResult title(const char*,bool underline,std::uint8_t& height);
    HeadWidgetResult progress(const DisplayRect&,std::uint16_t value,bool percentage); // 0x400e9ed4
    HeadWidgetResult temperature(const DisplayRect&,std::int32_t value,bool metric); // 0x400e8aac
    HeadWidgetResult duration(const DisplayRect&,std::uint32_t seconds); // 0x400eb49c/0x400eb268
    HeadWidgetResult strength(const DisplayRect&,std::uint32_t value); // 0x400e9cac
    HeadWidgetResult waiting(const DisplayRect&,const char* first,const char* second,bool animate,std::uint8_t frame); // 0x400e8b44
    HeadWidgetResult valve(const DisplayRect&,std::uint32_t location); // 0x400ee760
    HeadWidgetResult text_block(const DisplayRect&,const char* const*,std::uint8_t count,
        DisplayFontId,DisplayTextAlignment horizontal,std::uint32_t vertical,std::uint32_t padding=0); // 0x400e7ddc
private:
    HeadWidgetResult assertion(std::uint32_t);
    HeadWidgetResult rectangle(const DisplayRect&,std::uint8_t,bool filled=true);
    HeadWidgetResult text(const DisplayRect&,DisplayFontId,const char*,DisplayTextAlignment,
        std::int32_t row=0,std::int32_t offset_y=0,std::uint32_t characters=0,std::uint32_t spacing=1);
};
}} // namespace zseries::oem
