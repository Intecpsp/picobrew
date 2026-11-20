#include "zseries/head_presentation.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace zseries { namespace oem {
#define PRESENT_TRY(call) do{const auto result=(call);if(result!=HeadWidgetResult::Ok)return result;}while(false)
HeadWidgetResult HeadPresentation::assertion(const char* file,std::uint32_t line,const char* expression){
    painter.assertion_line=line;
    if(diagnostics&&diagnostics->assertion)return diagnostics->assertion(diagnostics->context,file,line,expression)?HeadWidgetResult::Ok:HeadWidgetResult::AssertionFailure;
    if(!painter.services.assertion)return HeadWidgetResult::MissingService;
    return painter.services.assertion(painter.services.context,line)?HeadWidgetResult::Ok:HeadWidgetResult::AssertionFailure;
}
HeadWidgetResult HeadPresentation::log(ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message){
    if(!diagnostics||!diagnostics->log)return HeadWidgetResult::MissingService;
    return diagnostics->log(diagnostics->context,level,tag,file,line,message)?HeadWidgetResult::Ok:HeadWidgetResult::AssertionFailure;
}
HeadWidgetResult HeadPresentation::add(HeadTextLines& lines,const char* format,...){
    if(!format)return HeadWidgetResult::InvalidText;
    if(lines.count>=lines.text.size())return log(ConsoleLevel::Error,"UI","TextLines.h",0x28,"Too many lines");
    auto& line=lines.text[lines.count];va_list args;va_start(args,format);const auto length=::vsnprintf(line.data(),line.size(),format,args);va_end(args);
    if(length<0)return HeadWidgetResult::InvalidText;
    if(length>24){char message[48];::snprintf(message,sizeof(message),"Line #%u truncated",static_cast<unsigned>(lines.count));PRESENT_TRY(log(ConsoleLevel::Error,"UI","TextLines.h",0x32,message));}
    ++lines.count;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadPresentation::text(const DisplayRect& rect,DisplayFontId id,const char* value,DisplayTextAlignment alignment,std::int32_t row,std::uint32_t spacing,bool invert){
    if(!value)return HeadWidgetResult::InvalidText;
    if(!*value){PRESENT_TRY(assertion("Display.h",0x3bf,"text && len > 0 && strlen(text) > 0"));return HeadWidgetResult::Ok;}
    const auto* font=display_font(id);DisplayTextContext context;
    if(!font||display_text_font(context,*font,spacing)!=DisplayTextResult::Ok||display_text_bounds(context,rect)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidRectangle;
    DisplayTextOptions options;options.alignment=alignment;options.row=row;options.invert=invert;
    return display_text(painter.display,context,value,std::strlen(value)+1,options)==DisplayTextResult::Ok?HeadWidgetResult::Ok:HeadWidgetResult::InvalidText;
}
HeadWidgetResult HeadPresentation::rectangle(const DisplayRect& rect,std::uint8_t color,bool filled){return display_rectangle(painter.display,rect,color,filled)==DisplayResult::Ok?HeadWidgetResult::Ok:HeadWidgetResult::InvalidRectangle;}
DisplayFontId HeadPresentation::select_font(const char* value,const DisplayRect& rect,bool large)const{
    std::uint32_t length=0;if(value)display_utf8_length(value,std::strlen(value)+1,length);
    const auto width=display_rect_width(rect);
    if(large&&length*17<=width)return DisplayFontId::Font17x20;
    return length*9<=width?DisplayFontId::Font9x11:DisplayFontId::Font6x8;
}
HeadWidgetResult HeadPresentation::centered(const char* value,const DisplayRect& rect,bool large,std::uint32_t vertical){
    if(!value||!*value){PRESENT_TRY(assertion("Graphics.h",0x4a,"!CString::isNullOrEmpty(text)"));return HeadWidgetResult::Ok;}
    const auto id=select_font(value,rect,large);const auto height=display_font(id)->glyph_height;
    DisplayRect area=rect;
    if(vertical==1)area.top=rect.top+static_cast<std::int32_t>((display_rect_height(rect)-height)/2);
    else if(vertical==2)area.top=rect.bottom-static_cast<std::int32_t>(height)+1;
    area.bottom=area.top+static_cast<std::int32_t>(height)-1;
    PRESENT_TRY(text(area,id,value));painter.dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadPresentation::lines(const HeadTextLines& lines,const DisplayRect& rect,DisplayFontId id,DisplayTextAlignment alignment){
    const auto* font=display_font(id);if(!font)return HeadWidgetResult::InvalidText;
    if(!font->glyph_height)PRESENT_TRY(assertion("Graphics.h",0x5d,"font.height > 0"));
    if(!lines.count)PRESENT_TRY(assertion("Graphics.h",0x5e,"lines.count > 0"));
    if(!font->glyph_height||!lines.count)return HeadWidgetResult::Ok;
    const auto height=display_rect_height(rect),content=lines.count*font->glyph_height;
    const auto padding=content<=height?(height-content)/(lines.count+1):0;
    const auto block=lines.count*(font->glyph_height+padding)-padding;
    DisplayRect bounds=rect;bounds.top+=static_cast<std::int32_t>((height-block)/2);bounds.bottom=bounds.top+static_cast<std::int32_t>(block)-1;
    for(std::uint32_t row=0;row<lines.count;++row)PRESENT_TRY(text(bounds,id,lines.text[row].data(),alignment,static_cast<std::int32_t>(row),padding));
    painter.dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadPresentation::title_body(const char* title,const HeadTextLines& lines,const DisplayRect& rect,bool underline){
    if(!title||!*title)PRESENT_TRY(assertion("Graphics.h",0x7b,"!CString::isNullOrEmpty(title)"));
    if(!lines.count)return centered(title,rect);
    DisplayRect body=rect;
    if(title&&*title){const auto id=select_font(title,rect);PRESENT_TRY(centered(title,rect,false,0));body.top+=static_cast<std::int32_t>(display_font(id)->glyph_height)+1;}
    if(underline)PRESENT_TRY(rectangle(body,15,false));
    return this->lines(lines,body);
}
#undef PRESENT_TRY
}} // namespace zseries::oem
