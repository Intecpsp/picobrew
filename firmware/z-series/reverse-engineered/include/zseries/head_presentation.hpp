// Recovered Graphics/TextLines helpers shared by the remaining Head pages.
#pragma once
#include "zseries/head_widgets.hpp"
#include "zseries/console.hpp"
#include <array>
namespace zseries { namespace oem {
struct HeadTextLines {std::array<std::array<char,25>,8> text{};std::uint32_t count=0;};
class HeadPresentation {
public:
    explicit HeadPresentation(HeadWidgetPainter& output,const ConsoleServices* diagnostics=nullptr)noexcept:painter(output),diagnostics(diagnostics){}
    HeadWidgetPainter& painter;
    const ConsoleServices* diagnostics;
    HeadWidgetResult assertion(const char* file,std::uint32_t,const char* expression);
    HeadWidgetResult log(ConsoleLevel,const char* tag,const char* file,std::uint32_t,const char*);
    HeadWidgetResult add(HeadTextLines&,const char* format,...);
    HeadWidgetResult text(const DisplayRect&,DisplayFontId,const char*,DisplayTextAlignment=DisplayTextAlignment::Center,std::int32_t row=0,std::uint32_t spacing=0,bool invert=false);
    HeadWidgetResult rectangle(const DisplayRect&,std::uint8_t,bool filled=true);
    DisplayFontId select_font(const char*,const DisplayRect&,bool large=false)const;
    HeadWidgetResult centered(const char*,const DisplayRect&,bool large=false,std::uint32_t vertical=1);
    HeadWidgetResult lines(const HeadTextLines&,const DisplayRect&,DisplayFontId=DisplayFontId::Font6x8,DisplayTextAlignment=DisplayTextAlignment::Center);
    HeadWidgetResult title_body(const char*,const HeadTextLines&,const DisplayRect&,bool underline);
};
}} // namespace zseries::oem
