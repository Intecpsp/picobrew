// Original Display console and its complete diagnostic demonstrations.
#pragma once
#include "zseries/console.hpp"
#include "zseries/display_effects.hpp"
#include "zseries/display_text.hpp"
#include "zseries/ui_catalog.hpp"

namespace zseries { namespace oem {
struct DisplayConsoleServices {
    void* context = nullptr;
    bool (*test_scope)(void*,bool begin,const char* tag,const char* file,
                       std::uint32_t line,const char* name) = nullptr;
};
class DisplayConsole {
public:
    DisplayConsole(DisplayState& display,DisplayTextContext& text_context,bool& dirty) noexcept;
    DisplayState& display;
    DisplayTextContext& text_context;
    bool& dirty;
    DisplayIo io;
    DisplayDelay delay;
    ConsoleServices diagnostics;
    DisplayConsoleServices services;
    DisplayPoint test_point{0,0};
    bool test_toggle = false;
    bool information();
    bool invoke(std::uint32_t original_callback,const char* argument);
    bool test(); // 0x400ea5a4
    bool text_modifier_test(); // 0x400e7fbc
    bool graphics_test(); // 0x400f1318
    bool test_message(const char*); // 0x400ee634, fixed centered 6x8 category/result
private:
    bool flush(bool pause=false);
    bool wait(std::uint32_t);
    bool scope(bool,const char*,std::uint32_t,const char* tag="DSP",const char* file="Display.h");
    bool text(const char*,std::int32_t row=0,std::uint32_t alignment=0,bool invert=false,
              std::int32_t offset_y=0,std::uint32_t characters=0);
    bool font(DisplayFontId,std::uint32_t spacing=0);
    bool bounds(const DisplayRect&);
    bool rect(const DisplayRect&,std::uint8_t color=15,bool filled=true);
    bool bitmap(UiBitmapId,DisplayPoint,const DisplayRect* region=nullptr);
    bool bitmap_test(UiBitmapId);
    bool corners(const DisplayRect&,UiBitmapId);
    bool gradient(const DisplayRect&,std::uint32_t direction,std::int32_t steps,bool white=true);
    bool alphabet(bool utf8);
    bool carets();
    bool gradients();
    bool screenshot();
    bool graphics_title(const char*,const DisplayRect&,std::uint32_t vertical);
    bool graphics_lines(const char* const*,std::uint32_t,const DisplayRect&,DisplayFontId);
    bool graphics_duration(const DisplayRect&,DisplayFontId,DisplayFontId,std::uint32_t);
};
}} // namespace zseries::oem
