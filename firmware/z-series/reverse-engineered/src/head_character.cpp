#include "zseries/head_character.hpp"
#include "zseries/display_effects.hpp"
#include <algorithm>
#include <cstring>
namespace zseries { namespace oem {
#define CHARACTER_TRY(value) do{const auto result=(value);if(result!=HeadCharacterResult::Ok)return result;}while(false)
HeadCharacterResult HeadCharacter::assertion(std::uint32_t line){painter.assertion_line=line;if(!painter.services.assertion)return HeadCharacterResult::MissingService;return painter.services.assertion(painter.services.context,line)?HeadCharacterResult::Ok:HeadCharacterResult::AssertionFailure;}
const char* HeadCharacter::characters()const noexcept{
    if(state.kind==1)return "0123456789ABCDEF";
    if(state.kind==2)return "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    return "0123456789";
}
bool HeadCharacter::cancel(std::int32_t index)const noexcept{return index==static_cast<std::int32_t>(state.character_count)+(state.kind==2?2:1);}
bool HeadCharacter::accept(std::int32_t index)const noexcept{return index==static_cast<std::int32_t>(state.character_count)+(state.kind==2?3:2);}
void HeadCharacter::reset()noexcept{selection=EncoderCharacter{};state.text.fill(0);state.length=0;state.truncated=false;}
HeadCharacterResult HeadCharacter::initialize(const DisplayRect& bounds,std::uint32_t kind,std::uint8_t numeric_limit,UiCommand response){
    if(numeric_limit>14){CHARACTER_TRY(assertion(0x39e));return HeadCharacterResult::Ok;}
    if(kind>2)return HeadCharacterResult::InvalidState;
    state=HeadCharacterState{};state.rect=bounds;state.kind=kind;state.response=response;
    selection.enabled=true;selection.selected=kind==0?1:0;
    state.character_count=kind==0?10:kind==1?16:94;selection.count=static_cast<std::int32_t>(state.character_count)+(kind==2?4:3);
    if(kind==2){state.main_font=state.secondary_font=DisplayFontId::Font6x8;state.visible=display_rect_width(bounds)/6-2;state.limit=42;}
    else{state.visible=display_rect_width(bounds)/17;state.limit=kind==1?14:numeric_limit;}
    return draw();
}
HeadCharacterResult HeadCharacter::rounded(const DisplayRect& rect){
    if(display_rectangle(painter.display,rect,15,true)!=DisplayResult::Ok)return HeadCharacterResult::RenderFailure;
    const auto* bitmap=ui_bitmap(UiBitmapId::SelectionBullet);if(!bitmap)return HeadCharacterResult::RenderFailure;
    const DisplayRect quarters[]={{0,0,3,3},{4,0,7,3},{0,4,3,7},{4,4,7,7}};
    const DisplayPoint points[]={{rect.left,rect.top},{rect.right-3,rect.top},{rect.left,rect.bottom-3},{rect.right-3,rect.bottom-3}};
    for(std::uint32_t i=0;i<4;++i)if(display_bitmap(painter.display,points[i],*bitmap,quarters[i],false)!=DisplayResult::Ok)return HeadCharacterResult::RenderFailure;
    return HeadCharacterResult::Ok;
}
HeadCharacterResult HeadCharacter::draw(){
    const auto& rect=state.rect;
    if(!display_rect_valid(rect)||!state.visible||state.visible>32||selection.count<=0||state.length>64||selection.selected<0||selection.selected>=selection.count)return HeadCharacterResult::InvalidState;
    const auto* main=display_font(state.main_font);const auto* secondary=display_font(state.secondary_font);if(!main||!secondary)return HeadCharacterResult::RenderFailure;
    if(display_rectangle(painter.display,rect,0,true)!=DisplayResult::Ok)return HeadCharacterResult::RenderFailure;
    const auto input_top=rect.bottom-static_cast<std::int32_t>((state.kind==2?2:1)*secondary->glyph_height);
    if(state.length){
        const auto columns=std::max<std::uint32_t>(1,display_rect_width(rect)/secondary->glyph_width);
        const auto rows=(state.length-1)/columns+1;
        DisplayTextContext context;const DisplayRect input{rect.left,rect.bottom-static_cast<std::int32_t>(secondary->glyph_height*rows),rect.right,rect.bottom};
        if(display_text_font(context,*secondary,1)!=DisplayTextResult::Ok||display_text_bounds(context,input)!=DisplayTextResult::Ok)return HeadCharacterResult::RenderFailure;
        for(std::uint32_t row=0;row<rows;++row){DisplayTextOptions options;options.row=static_cast<std::int32_t>(row);options.alignment=DisplayTextAlignment::Center;options.characters=row+1<rows?columns:0;
            if(display_text(painter.display,context,state.text.data()+row*columns,state.text.size()-row*columns,options)!=DisplayTextResult::Ok)return HeadCharacterResult::InvalidText;}
    }
    DisplayRect frame{0,input_top,static_cast<std::int32_t>(secondary->glyph_width*state.limit+1),rect.bottom};
    display_rect_translate(frame,static_cast<std::int32_t>((display_rect_width(rect)-display_rect_width(frame))/2),0);
    if(state.kind==1){if(state.limit!=14)CHARACTER_TRY(assertion(0x406));if(display_rectangle(painter.display,frame,8,false)!=DisplayResult::Ok)return HeadCharacterResult::RenderFailure;}
    else if(state.kind==0){if(display_rectangle(painter.display,{frame.left,frame.top,frame.right,frame.top},8,true)!=DisplayResult::Ok||display_rectangle(painter.display,{frame.left,frame.bottom,frame.right,frame.bottom},8,true)!=DisplayResult::Ok)return HeadCharacterResult::RenderFailure;}
    const DisplayRect picker{rect.left,rect.top,rect.right,input_top-1};
    const std::uint32_t picker_width=state.visible*main->glyph_width+9,picker_height=(state.kind==2?2*main->glyph_height+4:main->glyph_height)+1;
    if(display_rect_height(picker)<picker_height||display_rect_width(picker)<picker_width)return HeadCharacterResult::InvalidState;
    const auto left=static_cast<std::int32_t>((display_rect_width(picker)-picker_width)/2),top=static_cast<std::int32_t>((display_rect_height(picker)-picker_height)/2);
    DisplayTextContext context;if(display_text_font(context,*main,1)!=DisplayTextResult::Ok||display_text_bounds(context,rect)!=DisplayTextResult::Ok)return HeadCharacterResult::RenderFailure;
    const auto first=selection.selected-static_cast<std::int32_t>(state.visible/2);auto x=left;
    for(std::uint32_t i=0;i<state.visible;++i){const auto current=first+static_cast<std::int32_t>(i);const bool selected=current==selection.selected;
        auto index=current;if(index<0)index+=selection.count;else if(index>=selection.count)index-=selection.count;
        if(index<0||index>=selection.count)return HeadCharacterResult::InvalidState;
        if(selected){const DisplayRect highlight{x+1,rect.top+top-2,x+static_cast<std::int32_t>(main->glyph_width)+5,rect.top+top+static_cast<std::int32_t>(main->glyph_height)+2};CHARACTER_TRY(rounded(highlight));x+=4;}
        const char* label=nullptr;char glyph[3]={0,0,0};
        if(state.kind==2&&index==static_cast<std::int32_t>(state.character_count)){glyph[0]=' ';label=ui_localized_text(UiTextCategory::TextInput,0,painter.language);}
        else if(index==static_cast<std::int32_t>(state.character_count)+(state.kind==2?1:0)){glyph[0]=0x7f;label=ui_localized_text(UiTextCategory::TextInput,1,painter.language);}
        else if(cancel(index)){glyph[0]='\xc2';glyph[1]='\x83';label=ui_localized_text(UiTextCategory::TextInput,2,painter.language);}
        else if(accept(index)){glyph[0]='\xc2';glyph[1]='\x80';label=ui_localized_text(UiTextCategory::TextInput,3,painter.language);}
        else{if(static_cast<std::uint32_t>(index)>=state.character_count){CHARACTER_TRY(assertion(0x36a));return HeadCharacterResult::InvalidState;}glyph[0]=characters()[index];}
        DisplayTextOptions options;options.characters=1;options.offset_x=x;options.offset_y=top;options.invert=selected;
        if(display_text(painter.display,context,glyph,sizeof glyph,options)!=DisplayTextResult::Ok)return HeadCharacterResult::InvalidText;
        if(selected&&state.kind==2&&label&&*label){options=DisplayTextOptions{};options.row=2;options.alignment=DisplayTextAlignment::Center;options.offset_y=2;if(display_text(painter.display,context,label,std::strlen(label)+1,options)!=DisplayTextResult::Ok)return HeadCharacterResult::InvalidText;}
        x+=static_cast<std::int32_t>(main->glyph_width)+(selected?3:0);
    }
    if(display_horizontal_gradient(painter.display,{picker.left,picker.top,picker.left+14,picker.bottom},true,false)!=DisplayEffectResult::Ok||display_horizontal_gradient(painter.display,{picker.right-14,picker.top,picker.right,picker.bottom},true,true)!=DisplayEffectResult::Ok)return HeadCharacterResult::RenderFailure;
    return HeadCharacterResult::Ok;
}
HeadCharacterResult HeadCharacter::button(){
    if(!selection.enabled)return HeadCharacterResult::Ok;
    const auto index=selection.selected;if(index<0||index>=selection.count)return HeadCharacterResult::InvalidState;
    bool append_space=state.kind==2&&index==static_cast<std::int32_t>(state.character_count);
    if(!append_space&&index==static_cast<std::int32_t>(state.character_count)+(state.kind==2?1:0)){if(state.length){state.text[--state.length]=0;state.truncated=false;}}
    else if(accept(index)){
        if(state.kind==1&&state.length!=14)return HeadCharacterResult::Ok;
        if(!services.submit)return HeadCharacterResult::MissingService;
        selection.enabled=false;if(!services.submit(services.context,state.response,state.text))return HeadCharacterResult::ServiceFailure;
    }else if(cancel(index)){
        if(!services.cancel)return HeadCharacterResult::MissingService;
        selection.enabled=false;state.text.fill(0);state.length=0;state.truncated=false;
        if(!services.cancel(services.context))return HeadCharacterResult::ServiceFailure;
    }else{
        if(!append_space&&static_cast<std::uint32_t>(index)>=state.character_count){CHARACTER_TRY(assertion(0x46a));return HeadCharacterResult::InvalidState;}
        if(!append_space&&state.length>=state.limit)return HeadCharacterResult::Ok;
        const char value=append_space?' ':characters()[index];if(value){if(state.length<64){state.text[state.length++]=value;state.text[state.length]=0;}else state.truncated=true;}
    }
    CHARACTER_TRY(draw());painter.dirty=true;return HeadCharacterResult::Ok;
}
#undef CHARACTER_TRY
}} // namespace zseries::oem
