#include "zseries/head_widgets.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace zseries { namespace oem {
#define WIDGET_TRY(x) do{const auto result=(x);if(result!=HeadWidgetResult::Ok)return result;}while(false)
HeadWidgetResult HeadWidgetPainter::assertion(std::uint32_t line){assertion_line=line;if(!services.assertion)return HeadWidgetResult::MissingService;return services.assertion(services.context,line)?HeadWidgetResult::Ok:HeadWidgetResult::AssertionFailure;}
HeadWidgetResult HeadWidgetPainter::rectangle(const DisplayRect& rect,std::uint8_t color,bool filled){return display_rectangle(display,rect,color,filled)==DisplayResult::Ok?HeadWidgetResult::Ok:HeadWidgetResult::InvalidRectangle;}
HeadWidgetResult HeadWidgetPainter::text(const DisplayRect& rect,DisplayFontId font_id,const char* value,DisplayTextAlignment alignment,std::int32_t row,std::int32_t offset_y,std::uint32_t characters,std::uint32_t spacing){
    if(!value)return HeadWidgetResult::InvalidText;
    if(!*value){WIDGET_TRY(assertion(0x3bf));return HeadWidgetResult::Ok;}
    DisplayTextContext context;const auto* font=display_font(font_id);
    if(!font||display_text_font(context,*font,spacing)!=DisplayTextResult::Ok||display_text_bounds(context,rect)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidRectangle;
    DisplayTextOptions options;options.alignment=alignment;options.row=row;options.offset_y=offset_y;options.characters=characters;
    return display_text(display,context,value,std::strlen(value)+1,options)==DisplayTextResult::Ok?HeadWidgetResult::Ok:HeadWidgetResult::InvalidText;
}
HeadWidgetResult HeadWidgetPainter::title(const char* value,bool underline,std::uint8_t& height){
    height=0;if(!value||!*value){WIDGET_TRY(assertion(0x9b3));return HeadWidgetResult::Ok;}
    std::array<char,17> stored{};const std::size_t size=std::min<std::size_t>(std::strlen(value),16);std::memcpy(stored.data(),value,size);
    if(size>14){stored[13]='\xc2';stored[14]='\x84';stored[15]=0;}
    height=12;WIDGET_TRY(text(DisplayBounds,DisplayFontId::Font9x11,stored.data(),DisplayTextAlignment::Center));
    if(underline){WIDGET_TRY(rectangle({10,12,118,12},15));height+=2;}
    dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadWidgetPainter::progress(const DisplayRect& rect,std::uint16_t value,bool percentage){
    WIDGET_TRY(rectangle(rect,0));const std::uint32_t height=percentage?28:6;
    if(display_rect_height(rect)<height){WIDGET_TRY(assertion(0x8e0));return HeadWidgetResult::Ok;}
    std::int32_t top=display_rect_center_y(rect)-static_cast<std::int32_t>(height/2);
    if(percentage){std::array<char,8> label{};const std::size_t digits=value==100?3:value<10?1:2;::snprintf(label.data(),label.size(),"%u%%",static_cast<unsigned>(value));label[digits+1]=0;WIDGET_TRY(text(DisplayBounds,DisplayFontId::Font14x16,label.data(),DisplayTextAlignment::Center,0,top));top+=22;}
    DisplayRect bar{rect.left+5,top,rect.right-5,top+6};WIDGET_TRY(rectangle(bar,15,false));
    const std::uint32_t width=value?static_cast<std::uint32_t>(value)*display_rect_width(bar)/100:0;
    bar.right=bar.left+static_cast<std::int32_t>(width);return rectangle(bar,15,true);
}
HeadWidgetResult HeadWidgetPainter::temperature(const DisplayRect& rect,std::int32_t value,bool metric){
    WIDGET_TRY(rectangle(rect,0));std::array<char,10> label{};::snprintf(label.data(),label.size(),"%d\xc2\xb0%c",static_cast<int>(value),metric?'C':'F');
    return text(rect,DisplayFontId::Font14x16,label.data(),DisplayTextAlignment::Center,0,13);
}
HeadWidgetResult HeadWidgetPainter::duration(const DisplayRect& rect,std::uint32_t seconds){
    WIDGET_TRY(rectangle(rect,0));constexpr std::uint32_t height=29;
    if(display_rect_height(rect)<height)WIDGET_TRY(assertion(0xf8));
    const std::uint32_t top_bits=(display_rect_height(rect)-height)/2+static_cast<std::uint32_t>(rect.top);
    std::int32_t top;std::memcpy(&top,&top_bits,sizeof top);
    std::array<char,16> label{};::snprintf(label.data(),label.size(),"%02u:%02u",static_cast<unsigned>(seconds/3600),static_cast<unsigned>((seconds/60)%60));
    const auto width=static_cast<std::uint32_t>(std::strlen(label.data()))*17;
    if(display_rect_width(rect)<=width)WIDGET_TRY(assertion(0x103));
    const auto left=static_cast<std::int32_t>((display_rect_width(rect)-width)/2);
    DisplayRect number{left,top+9,left+static_cast<std::int32_t>(width)-1,top+28};
    DisplayRect tag{left+2,top,left+static_cast<std::int32_t>(width)+1,top+19},clipped{};
    if(!display_rect_intersection(tag,rect,clipped))return HeadWidgetResult::InvalidRectangle;
    WIDGET_TRY(text(clipped,DisplayFontId::Font6x8,"HR",DisplayTextAlignment::Left));
    display_rect_translate(tag,51,0);if(!display_rect_intersection(tag,rect,clipped))return HeadWidgetResult::InvalidRectangle;
    WIDGET_TRY(text(clipped,DisplayFontId::Font6x8,"MIN",DisplayTextAlignment::Left));
    WIDGET_TRY(text(number,DisplayFontId::Font17x20,label.data(),DisplayTextAlignment::Left));dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadWidgetPainter::strength(const DisplayRect& rect,std::uint32_t value){
    WIDGET_TRY(rectangle(rect,0));const DisplayRect bar{3,32,124,33};WIDGET_TRY(rectangle(bar,0));
    for(std::uint32_t i=0;i<10;++i){const auto left=static_cast<std::int32_t>(3+12*i);WIDGET_TRY(rectangle({left,32,left+9,33},15,i<value));}
    WIDGET_TRY(text(rect,DisplayFontId::Font6x8,ui_localized_text(UiTextCategory::TextCommon,2,language),DisplayTextAlignment::Left,3));
    return text(rect,DisplayFontId::Font6x8,ui_localized_text(UiTextCategory::TextCommon,3,language),DisplayTextAlignment::Right,3);
}
HeadWidgetResult HeadWidgetPainter::waiting(const DisplayRect& rect,const char* first,const char* second,bool animate,std::uint8_t frame){
    if(!first||!*first)return HeadWidgetResult::Ok;
    WIDGET_TRY(rectangle(rect,0));const bool detail=second&&*second;const std::uint32_t height=8+(animate?18:0)+(detail?9:0);
    std::int32_t y=display_rect_center_y(rect)-static_cast<std::int32_t>(height/2);
    WIDGET_TRY(text(DisplayBounds,DisplayFontId::Font6x8,first,DisplayTextAlignment::Center,0,y));y+=9;
    if(detail){WIDGET_TRY(text(DisplayBounds,DisplayFontId::Font6x8,second,DisplayTextAlignment::Center,0,y));y+=9;}
    if(animate){static const char spinner[]="/-\\|";if(frame>4)return HeadWidgetResult::InvalidText;WIDGET_TRY(text(DisplayBounds,DisplayFontId::Font6x8,spinner+frame,DisplayTextAlignment::Center,0,y+9,1));}
    dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadWidgetPainter::valve(const DisplayRect& rect,std::uint32_t location){
    WIDGET_TRY(rectangle(rect,0));if(location>6){WIDGET_TRY(assertion(0x614));location=0;}
    const auto left=rect.left+static_cast<std::int32_t>((display_rect_width(rect)-33)/2);const auto top=rect.bottom-32;
    DisplayRect label{rect.left,rect.top,rect.right,top-1};const auto* name=to_string(static_cast<ValveLocation>(location));
    const auto label_y=label.top+static_cast<std::int32_t>((display_rect_height(label)-8)/2);DisplayRect text_rect{label.left,label_y,label.right,label_y+7};
    WIDGET_TRY(text(text_rect,DisplayFontId::Font6x8,name,DisplayTextAlignment::Center,0,0,0,0));
    WIDGET_TRY(rectangle({left,top,left+32,top+32},15,false));
    const int selected=location==3?3:location==4?2:location==5?1:location==6?0:-1;
    for(int i=0;i<4;++i){const auto x=left+8*i;WIDGET_TRY(rectangle({x,top,x+8,top+8},15,false));if(i==selected)WIDGET_TRY(rectangle({x+2,top+2,x+6,top+6},15,true));}
    if(location==2)WIDGET_TRY(rectangle({left+2,top+10,left+30,top+30},15,true));
    dirty=true;return HeadWidgetResult::Ok;
}
HeadWidgetResult HeadWidgetPainter::text_block(const DisplayRect& rect,const char* const* lines,std::uint8_t count,DisplayFontId font_id,DisplayTextAlignment horizontal,std::uint32_t vertical,std::uint32_t padding){
    const auto* font=display_font(font_id);if(!font||!lines)return HeadWidgetResult::InvalidText;
    const std::uint32_t block=(font->glyph_height+1)*count,height=display_rect_height(rect);
    if(!count||height<block||!lines[0]||!*lines[0])return HeadWidgetResult::Ok;
    DisplayRect bounds=rect;
    if(vertical==2)bounds.top=rect.bottom-static_cast<std::int32_t>(padding)-static_cast<std::int32_t>(block);
    else if(vertical==0||vertical==1){if(vertical==1)padding=(height-block-1)/2;bounds.top+=static_cast<std::int32_t>(padding);bounds.bottom=bounds.top+static_cast<std::int32_t>(block);}
    const std::uint32_t columns=(display_rect_width(bounds)-4)/font->glyph_width;
    for(std::uint32_t i=0;i<count;++i){
        if(!lines[i])return HeadWidgetResult::InvalidText;
        std::uint32_t length=0;if(display_utf8_length(lines[i],std::strlen(lines[i])+1,length)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;
        const char* value=lines[i];std::string shortened;
        if(length>columns+1){DisplayUtf8 decoder;if(display_utf8_initialize(decoder,value,std::strlen(value)+1)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;for(std::uint32_t j=0;j<columns;++j){std::uint32_t codepoint;if(display_utf8_next(decoder,codepoint)!=DisplayTextResult::Ok)return HeadWidgetResult::InvalidText;}shortened.assign(value,decoder.cursor);shortened+="\xc2\x84";value=shortened.c_str();}
        WIDGET_TRY(text(bounds,font_id,value,horizontal,static_cast<std::int32_t>(i)));
    }
    dirty=true;return HeadWidgetResult::Ok;
}
#undef WIDGET_TRY
}} // namespace zseries::oem
