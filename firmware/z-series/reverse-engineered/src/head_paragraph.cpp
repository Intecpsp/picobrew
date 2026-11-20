#include "zseries/head_paragraph.hpp"
#include "zseries/display_effects.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
#define PARAGRAPH_TRY(x) do {const auto result=(x);if(result!=HeadParagraphResult::Ok)return result;}while(false)
bool HeadScrollData::append(const char* value) {
    if(!value||count>=lines.size())return false;
    auto& line=lines[count++];line.fill(0);std::memcpy(line.data(),value,std::min<std::size_t>(64,std::strlen(value)));return true;
}
HeadParagraphResult HeadParagraphRenderer::initialize(const DisplayRect& rect,DisplayTextAlignment alignment,
                                                     UiCommand command,const char* button,bool reserve_bottom) {
    if(!services.millis)return HeadParagraphResult::MissingService;
    state.rect=rect;if(reserve_bottom)state.rect.bottom-=9;
    const auto height=display_rect_height(state.rect);
    if(height<5)return HeadParagraphResult::InvalidState;
    selection.visible_lines=static_cast<std::int32_t>((height-5)/8);
    if(button&&*button)--selection.visible_lines;
    if(selection.visible_lines<0)return HeadParagraphResult::InvalidState;
    selection.offset=0;selection.enabled=true;selection.total_lines=static_cast<std::int32_t>(data.count);
    state.alignment=alignment;state.command=command;state.button.fill(0);
    if(button)std::memcpy(state.button.data(),button,std::min<std::size_t>(128,std::strlen(button)));
    state.caret_visible=false;state.deadline=services.millis(services.context)+state.interval_ms;
    const bool has_button=command!=UiCommand::None&&state.button[0];
    const auto center=state.rect.left<=state.rect.right?(state.rect.left+state.rect.right)/2:0;
    const auto bottom=state.rect.bottom-(has_button?9:0);
    state.up={center-2,state.rect.top-1,center+2,state.rect.top+3};
    state.down={center-2,bottom-3,center+2,bottom-1};
    DisplayRect content=state.rect;if(has_button)content.bottom-=9;
    state.text_offset=static_cast<std::uint32_t>(display_rect_center_y(content)-selection.visible_lines*4-state.rect.top);
    PARAGRAPH_TRY(draw());return caret();
}
HeadParagraphResult HeadParagraphRenderer::text(const DisplayRect& rect,const char* value,std::uint32_t row,
                                               bool invert,DisplayTextAlignment alignment) {
    DisplayTextContext context;const auto* font=display_font(DisplayFontId::Font6x8);
    if(!font||display_text_font(context,*font,0)!=DisplayTextResult::Ok||display_text_bounds(context,rect)!=DisplayTextResult::Ok)return HeadParagraphResult::RectangleFailure;
    DisplayTextOptions options;options.row=static_cast<std::int32_t>(row);options.invert=invert;options.alignment=alignment;
    return display_text(painter.display,context,value,std::strlen(value)+1,options)==DisplayTextResult::Ok?HeadParagraphResult::Ok:HeadParagraphResult::InvalidText;
}
HeadParagraphResult HeadParagraphRenderer::draw() {
    if(!selection.visible_lines)return HeadParagraphResult::Ok;
    const DisplayRect content{state.rect.left,state.rect.top+static_cast<std::int32_t>(state.text_offset),state.rect.right,state.down.top-1};
    if(display_rectangle(painter.display,content,0,true)!=DisplayResult::Ok)return HeadParagraphResult::RectangleFailure;
    for(std::uint32_t row=0;row<static_cast<std::uint32_t>(selection.visible_lines);++row){
        const auto index=static_cast<std::uint32_t>(selection.offset)+row;if(index>=data.count)break;
        if(data.lines[index][0])PARAGRAPH_TRY(text(content,data.lines[index].data(),row,false,state.alignment));
    }
    if(state.command!=UiCommand::None&&state.button[0]){
        DisplayRect button{state.rect.left+10,state.rect.bottom-8,state.rect.right-10,state.rect.bottom};
        if(display_rectangle(painter.display,button,15,true)!=DisplayResult::Ok)return HeadParagraphResult::RectangleFailure;
        const auto* bitmap=ui_bitmap(UiBitmapId::SelectionBullet);if(!bitmap)return HeadParagraphResult::BitmapFailure;
        const DisplayPoint positions[]={{button.left,button.top},{button.left,button.bottom-3},{button.right-3,button.bottom-3},{button.right-3,button.top}};
        const DisplayRect quarters[]={{0,0,3,3},{0,4,3,7},{4,4,7,7},{4,0,7,3}};
        for(std::uint32_t i=0;i<4;++i)if(display_bitmap(painter.display,positions[i],*bitmap,quarters[i])!=DisplayResult::Ok)return HeadParagraphResult::BitmapFailure;
        PARAGRAPH_TRY(text(button,state.button.data(),0,true,DisplayTextAlignment::Center));
    }
    painter.dirty=true;return HeadParagraphResult::Ok;
}
HeadParagraphResult HeadParagraphRenderer::caret() {
    if(!state.caret_visible){
        if(selection.offset&&static_cast<std::uint32_t>(selection.visible_lines)<data.count&&
           display_caret(painter.display,{state.up.left,state.up.top+2},3,1,3)!=DisplayEffectResult::Ok)return HeadParagraphResult::RectangleFailure;
        if(static_cast<std::uint32_t>(selection.visible_lines+selection.offset)!=data.count&&static_cast<std::uint32_t>(selection.visible_lines)<data.count&&
           display_caret(painter.display,{state.down.left,state.down.top},3,1,2)!=DisplayEffectResult::Ok)return HeadParagraphResult::RectangleFailure;
    }else if(display_rectangle(painter.display,state.up,0,true)!=DisplayResult::Ok||display_rectangle(painter.display,state.down,0,true)!=DisplayResult::Ok)return HeadParagraphResult::RectangleFailure;
    state.caret_visible=!state.caret_visible;painter.dirty=true;return HeadParagraphResult::Ok;
}
#undef PARAGRAPH_TRY
}} // namespace zseries::oem
