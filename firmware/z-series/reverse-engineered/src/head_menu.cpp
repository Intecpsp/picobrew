#include "zseries/head_menu.hpp"
#include "zseries/display_effects.hpp"
#include <algorithm>
#include <cstring>
#include <string>

namespace zseries { namespace oem {
namespace {
std::int32_t signed_word(std::uint32_t value) noexcept {
    std::int32_t result;std::memcpy(&result,&value,sizeof result);return result;
}
void copy_label(std::array<char,129>& destination,const char* label) {
    destination.fill(0);
    if(label)std::memcpy(destination.data(),label,std::min<std::size_t>(128,std::strlen(label)));
}
}
#define MENU_TRY(x) do { const auto result=(x);if(result!=HeadMenuResult::Ok)return result; } while(false)

void HeadMenuData::clear() noexcept {count=0;for(auto& entry:items)entry=HeadMenuItem{};}
bool HeadMenuData::append(const HeadUiEvent& event,const char* label) {
    if(count>=items.size())return false;
    auto& entry=items[count++];entry.event=event;copy_label(entry.text,label);return true;
}
bool HeadMenuData::append(UiCommand command,const char* label) {
    HeadUiEvent event;event.command=command;
    return append(event,label?label:ui_localized_text(UiTextCategory::UiCommand,static_cast<std::uint32_t>(command),language));
}
bool HeadMenuData::prepend(UiCommand command,const char* label) {
    if(count>=items.size())return false;
    for(std::uint32_t i=count;i>0;--i)items[i]=items[i-1];
    items[0]=HeadMenuItem{};items[0].event.command=command;
    copy_label(items[0].text,label?label:ui_localized_text(UiTextCategory::UiCommand,static_cast<std::uint32_t>(command),language));
    ++count;return true;
}
HeadMenuResult HeadMenuRenderer::assertion(std::uint32_t line) {
    painter.assertion_line=line;
    if(!painter.services.assertion)return HeadMenuResult::MissingService;
    return painter.services.assertion(painter.services.context,line)?HeadMenuResult::Ok:HeadMenuResult::AssertionFailure;
}
HeadMenuResult HeadMenuRenderer::rectangle(const DisplayRect& rect,std::uint8_t color) {
    return display_rectangle(painter.display,rect,color,true)==DisplayResult::Ok?HeadMenuResult::Ok:HeadMenuResult::RectangleFailure;
}
HeadMenuResult HeadMenuRenderer::compose(DisplayRect& available,const char* title,std::uint32_t maximum_rows,
                                       const HeadMenuBody* body,std::uint32_t vertical,UiCommand default_command) {
    if(!maximum_rows)MENU_TRY(assertion(0x236));
    const bool has_title=title&&*title;
    const std::uint32_t possible=(display_rect_height(available)-(has_title?10u:0u))/9;
    const auto provisional=std::min(maximum_rows,std::min(data.count,possible));
    const std::uint8_t rows=provisional>=5&&data.count>=6?5:provisional>=3&&data.count>=3?3:1;
    if(has_title){
        if(display_rect_height(available)!=64||default_command!=UiCommand::None)MENU_TRY(assertion(0x1f0));
        std::uint8_t height=0;
        if(painter.title(title,rows==1||body,height)!=HeadWidgetResult::Ok)return HeadMenuResult::WidgetFailure;
        available.top+=height;
    }
    std::uint8_t shift=0;
    if(vertical==1)shift=static_cast<std::uint8_t>((available.bottom-available.top-rows*9)/2);
    DisplayRect bounds{10,63-rows*9-shift,118,63-shift};
    MENU_TRY(initialize(bounds,rows));available.bottom=bounds.top-1;
    if(body&&body->count&&painter.text_block(available,body->lines,body->count,DisplayFontId::Font6x8,body->horizontal,body->vertical)!=HeadWidgetResult::Ok)return HeadMenuResult::WidgetFailure;
    return HeadMenuResult::Ok;
}
HeadMenuResult HeadMenuRenderer::initialize(const DisplayRect& rect,std::uint8_t rows) {
    state.rect=rect;selection.enabled=true;selection.selection_row=0;
    state.mode=rows<3?0:rows<5?1:2;selection.visible_rows=state.mode*2+1;selection.selection_row=state.mode;
    selection.count=data.count;
    if(!data.count){
        // The original returns UINT_MAX from getCommand and reads before its
        // item array after these diagnostics. Preserve diagnostics, reject the
        // out-of-bounds access at the ownership boundary.
        MENU_TRY(assertion(0x124));MENU_TRY(assertion(0x126));return HeadMenuResult::InvalidState;
    }
    selection.selected=data.items[0].event.command==UiCommand::Back&&data.count>1?1:0;
    selection.scroll_offset=selection.selected-selection.selection_row;
    state.last_visible=std::min(data.count,selection.visible_rows)-1;
    state.characters=static_cast<std::uint32_t>(rect.right-rect.left-14)/6;
    if(!state.mode)state.characters-=2;
    auto& marquee=state.marquee;marquee.width=(state.characters+1)*6;
    marquee.point={64-static_cast<std::int32_t>(marquee.width/2),rect.top+static_cast<std::int32_t>(selection.selection_row*9)};
    MENU_TRY(draw());painter.dirty=true;return HeadMenuResult::Ok;
}
HeadMenuResult HeadMenuRenderer::rounded_selection(const DisplayRect& rect) {
    MENU_TRY(rectangle(rect,15));const auto* bitmap=ui_bitmap(UiBitmapId::SelectionBullet);
    if(!bitmap)return HeadMenuResult::BitmapFailure;
    DisplayRect quarter{0,0,3,3};
    const DisplayPoint positions[]={{rect.left,rect.top},{rect.left,rect.bottom-3},{rect.right-3,rect.bottom-3},{rect.right-3,rect.top}};
    const DisplayRect quarters[]={{0,0,3,3},{0,4,3,7},{4,4,7,7},{4,0,7,3}};
    for(std::uint32_t i=0;i<4;++i){quarter=quarters[i];if(display_bitmap(painter.display,positions[i],*bitmap,quarter)!=DisplayResult::Ok)return HeadMenuResult::BitmapFailure;}
    return HeadMenuResult::Ok;
}
HeadMenuResult HeadMenuRenderer::draw_text(const char* text,std::int32_t row,bool invert) {
    if(!*text){MENU_TRY(assertion(0x3bf));return HeadMenuResult::Ok;}
    DisplayTextContext context;const auto* font=display_font(DisplayFontId::Font6x8);
    if(!font||display_text_font(context,*font,1)!=DisplayTextResult::Ok||display_text_bounds(context,state.rect)!=DisplayTextResult::Ok)return HeadMenuResult::RectangleFailure;
    DisplayTextOptions options;options.row=row;options.invert=invert;options.alignment=DisplayTextAlignment::Center;
    return display_text(painter.display,context,text,std::strlen(text)+1,options)==DisplayTextResult::Ok?HeadMenuResult::Ok:HeadMenuResult::InvalidText;
}
HeadMenuResult HeadMenuRenderer::prepare_marquee(const char* text) {
    DisplayUtf8 decoder;if(display_utf8_initialize(decoder,text,std::strlen(text)+1)!=DisplayTextResult::Ok)return HeadMenuResult::InvalidText;
    auto& marquee=state.marquee;marquee.full_width=decoder.characters*6;
    if(marquee.full_width>512)return HeadMenuResult::InvalidText;
    marquee.pixels.fill(0);const auto* font=display_font(DisplayFontId::Font6x8);if(!font)return HeadMenuResult::BitmapFailure;
    for(std::uint32_t i=0;i<decoder.characters;++i){
        std::uint32_t codepoint;if(display_utf8_next(decoder,codepoint)!=DisplayTextResult::Ok)return HeadMenuResult::InvalidText;
        if(codepoint==9||codepoint==10)codepoint=32;
        if(codepoint<32||codepoint>255)codepoint=256;
        const auto source=(codepoint-32)*3,destination=i*3;
        for(std::uint32_t row=0;row<8;++row)std::memcpy(marquee.pixels.data()+row*256+destination,font->bitmap.pixels+row*font->bitmap.row_bytes+source,3);
    }
    marquee.active=true;marquee.source_x=0;return HeadMenuResult::Ok;
}
HeadMenuResult HeadMenuRenderer::draw_marquee() {
    auto& marquee=state.marquee;
    // The original constructor pairs a 512-pixel backing width with inclusive
    // descriptor bounds ending at 255. Retain both independently.
    DisplayBitmap bitmap{512,15,256,{0,0,255,14},marquee.pixels.data(),marquee.pixels.size()};
    DisplayRect crop{static_cast<std::int32_t>(marquee.source_x),7,static_cast<std::int32_t>(marquee.source_x+marquee.width-1),14};
    return display_bitmap(painter.display,marquee.point,bitmap,crop,true)==DisplayResult::Ok?HeadMenuResult::Ok:HeadMenuResult::BitmapFailure;
}
HeadMenuResult HeadMenuRenderer::item(std::uint32_t index,std::int32_t row,bool selected) {
    if(index>=data.count)return HeadMenuResult::InvalidState;
    const char* value=data.items[index].text.data();std::uint32_t length=0;
    if(display_utf8_length(value,std::strlen(value)+1,length)!=DisplayTextResult::Ok)return HeadMenuResult::InvalidText;
    if(length<=state.characters+1)return draw_text(value,row,selected);
    if(selected){
        if(!services.millis)return HeadMenuResult::MissingService;
        MENU_TRY(prepare_marquee(value));MENU_TRY(draw_marquee());
        state.marquee.deadline=services.millis(services.context)+state.marquee.step_ms;return HeadMenuResult::Ok;
    }
    DisplayUtf8 decoder;if(display_utf8_initialize(decoder,value,std::strlen(value)+1)!=DisplayTextResult::Ok)return HeadMenuResult::InvalidText;
    for(std::uint32_t i=0;i<state.characters;++i){std::uint32_t codepoint;if(display_utf8_next(decoder,codepoint)!=DisplayTextResult::Ok)return HeadMenuResult::InvalidText;}
    std::string shortened(value,decoder.cursor);shortened+="\xc2\x84";return draw_text(shortened.c_str(),row,false);
}
HeadMenuResult HeadMenuRenderer::draw() {
    state.marquee.deadline=0;state.marquee.active=false;
    if(!selection.visible_rows||!data.count)return HeadMenuResult::Ok;
    MENU_TRY(rectangle(state.rect,0));
    if(state.mode>2)return HeadMenuResult::Ok;
    const std::uint32_t selected_row=state.mode?selection.selection_row:selection.selected-selection.scroll_offset;
    const auto top=state.rect.top+static_cast<std::int32_t>(selected_row*9);
    MENU_TRY(rounded_selection({state.rect.left,top,state.rect.right,top+8}));
    if(!state.mode){
        state.gradient=false;
        UiBitmapId arrow=data.count==1?UiBitmapId::SelectionSingle:selection.selected==0?UiBitmapId::SelectionDown:selection.selected<data.count-1?UiBitmapId::SelectionBoth:UiBitmapId::SelectionUp;
        const auto* bitmap=ui_bitmap(arrow);if(!bitmap)return HeadMenuResult::BitmapFailure;
        DisplayPoint position{state.rect.right-static_cast<std::int32_t>(bitmap->width)-4,top+static_cast<std::int32_t>((9-bitmap->height)/2)};
        if(display_bitmap(painter.display,position,*bitmap,bitmap->bounds)!=DisplayResult::Ok)return HeadMenuResult::BitmapFailure;
        return item(selection.selected,static_cast<std::int32_t>(selected_row),true);
    }
    state.gradient=true;state.gradient_steps=state.mode==1?6:14;
    for(std::uint32_t row=0;row<selection.visible_rows;++row){
        const auto index=selection.scroll_offset+row;
        if(signed_word(index)>=0&&index<data.count)MENU_TRY(item(index,static_cast<std::int32_t>(row),selection.selected==index));
    }
    MENU_TRY(rectangle({state.rect.left+10,state.rect.top-1,state.rect.right-10,state.rect.top-1},15));
    MENU_TRY(rectangle({state.rect.left+10,state.rect.bottom,state.rect.right-10,state.rect.bottom},15));
    const auto steps=static_cast<std::int32_t>(state.gradient_steps);
    if(display_vertical_gradient(painter.display,{state.rect.left,state.rect.top+1,state.rect.right,state.rect.top+1+steps},false)!=DisplayEffectResult::Ok||
       display_vertical_gradient(painter.display,{state.rect.left,state.rect.bottom-2-steps,state.rect.right,state.rect.bottom-2},true)!=DisplayEffectResult::Ok)return HeadMenuResult::RectangleFailure;
    return HeadMenuResult::Ok;
}
HeadMenuResult HeadMenuRenderer::tick(std::uint32_t now) {
    auto& marquee=state.marquee;
    if(!marquee.active||marquee.deadline>=now)return HeadMenuResult::Ok;
    if(marquee.full_width<=marquee.source_x+marquee.width){marquee.source_x=0;marquee.deadline=now+marquee.pause_ms;return HeadMenuResult::Ok;}
    MENU_TRY(draw_marquee());painter.dirty=true;
    marquee.deadline=now+(marquee.source_x?marquee.step_ms:marquee.pause_ms);++marquee.source_x;return HeadMenuResult::Ok;
}
#undef MENU_TRY
}} // namespace zseries::oem
