#include "zseries/head_screens.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace zseries { namespace oem {
namespace {
bool whitespace(std::uint32_t value){return value==32||value==9||value==10;}
template<std::size_t N> void copy(std::array<char,N>& output,const char* value){
    output.fill(0);if(value)std::memcpy(output.data(),value,std::min<std::size_t>(N-1,std::strlen(value)));
}
std::int32_t temperature_integer(float value){
    if(std::isnan(value)||value<=static_cast<float>(std::numeric_limits<std::int32_t>::min()))return std::numeric_limits<std::int32_t>::min();
    if(value>=static_cast<float>(std::numeric_limits<std::int32_t>::max()))return std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int32_t>(value);
}
bool append_wrapped(HeadWrappedText& output,const char* value,std::size_t count){
    if(output.count>=output.lines.size())return false;
    auto& line=output.lines[output.count++];line.fill(0);std::memcpy(line.data(),value,std::min<std::size_t>(42,count));return true;
}
}
bool head_wrap_text(HeadWrappedText& output,const char* text,std::uint32_t horizontal_padding,std::uint32_t glyph_width,std::uint32_t maximum_lines){
    output=HeadWrappedText{};if(!text||!*text)return true;
    if(horizontal_padding>=128||!glyph_width||!maximum_lines||maximum_lines>12)return false;
    const auto columns=(128-horizontal_padding)/glyph_width;if(!columns)return false;
    DisplayUtf8 decoder;if(display_utf8_initialize(decoder,text,std::strlen(text)+1)!=DisplayTextResult::Ok)return false;
    std::uint32_t start=0,last_space=0,line=0;
    const auto prefix=[&](std::uint32_t position,std::uint32_t count,std::uint32_t& bytes){
        DisplayUtf8 part;if(display_utf8_initialize(part,text+position,std::strlen(text+position)+1)!=DisplayTextResult::Ok)return false;
        for(std::uint32_t i=0;i<count&&part.cursor<part.length;++i){std::uint32_t cp;if(display_utf8_next(part,cp)!=DisplayTextResult::Ok)return false;}
        bytes=part.cursor;return true;
    };
    while(decoder.cursor<decoder.length){
        std::uint32_t characters=0;
        while(characters<columns&&decoder.cursor<decoder.length){
            std::uint32_t cp;if(display_utf8_next(decoder,cp)!=DisplayTextResult::Ok)return false;
            if(whitespace(cp)){last_space=decoder.cursor-1;if(cp==10)break;}
            characters=(characters+1)&255;
        }
        if(decoder.cursor>=decoder.length)return append_wrapped(output,text+start,decoder.length-start);
        if(line==maximum_lines-1){
            std::uint32_t bytes;if(!prefix(start,columns-1,bytes))return false;
            std::string shortened(text+start,bytes);shortened+="\xc2\x84";
            if(!append_wrapped(output,shortened.c_str(),shortened.size()+1))return false;
            output.truncated=true;return true;
        }
        if(start<last_space){
            std::uint32_t cp;if(display_utf8_next(decoder,cp)!=DisplayTextResult::Ok)return false;
            if(whitespace(cp))last_space=decoder.cursor-1;
            if(last_space<start||!append_wrapped(output,text+start,last_space-start))return false;
            start=last_space+1;
        }else{
            std::uint32_t bytes;if(!prefix(start,columns,bytes)||!append_wrapped(output,text+start,bytes))return false;
            start+=bytes;std::uint32_t cp='X';
            while(true){
                if(decoder.cursor>=decoder.length)return true;
                if(whitespace(cp))break;
                if(display_utf8_next(decoder,cp)!=DisplayTextResult::Ok)return false;
                ++start;
            }
        }
        if(start>decoder.length||(static_cast<unsigned char>(text[start])&0xc0)==0x80)return false;
        decoder.cursor=static_cast<std::uint16_t>(start);++line;
    }
    return true;
}

#define SCREEN_TRY(x) do {const auto result=(x);if(result!=HeadScreenResult::Ok)return result;}while(false)
#define PAINT_TRY(x) do {if((x)!=HeadWidgetResult::Ok)return HeadScreenResult::RenderFailure;}while(false)
#define MENU_RESULT(x) do {if((x)!=HeadMenuResult::Ok)return HeadScreenResult::RenderFailure;}while(false)
#define PARAGRAPH_RESULT(x) do {if((x)!=HeadParagraphResult::Ok)return HeadScreenResult::RenderFailure;}while(false)
#define QUANTITY_RESULT(x) do {if((x)!=HeadQuantityResult::Ok)return HeadScreenResult::RenderFailure;}while(false)
#define CHARACTER_RESULT(x) do {if((x)!=HeadCharacterResult::Ok)return HeadScreenResult::RenderFailure;}while(false)

HeadScreens::HeadScreens(DisplayState& display,HeadEncoderState& encoder,UiScreenTickState& periodic) noexcept
    :display(display),encoder(encoder),periodic(periodic),painter(display),menu(painter,menu_data,encoder.menu),paragraph(painter,scroll_data,encoder.paragraph),character(painter,encoder.character),quantity(painter,encoder.quantities[1]),dynamic(painter,observed),hardware(painter,observed),still(painter,observed),slideshow(painter,observed,encoder.slideshow){
    encoder.temperature.minimum=50;encoder.temperature.maximum=200;periodic.waiting.frame_count=4;
    bind();
}
void HeadScreens::bind() noexcept {
    dynamic.text_context=text_context;
    slideshow.services.context=this;slideshow.services.millis=[](void* p){auto& owner=*static_cast<HeadScreens*>(p);return owner.services.millis(owner.services.context);};
    slideshow.services.post=[](void* p,UiCommand command){HeadScreenEvent value;value.ui.command=command;return static_cast<HeadScreens*>(p)->event(value)==HeadScreenResult::Ok;};
    painter.language=language;menu_data.language=language;
    painter.services.context=services.context;painter.services.assertion=services.assertion;
    menu.services.context=services.context;menu.services.millis=services.millis;
    paragraph.services.context=services.context;paragraph.services.millis=services.millis;
    quantity.services.context=this;
    quantity.services.submit=[](void* p,HeadQuantityId id,Quantity value){auto& screens=*static_cast<HeadScreens*>(p);HeadScreenEvent selected;selected.kind=HeadScreenEventKind::Quantity;selected.quantity_id=id;selected.quantity=value;return screens.event(selected)==HeadScreenResult::Ok;};
    character.services.context=this;
    character.services.cancel=[](void* p){auto& screens=*static_cast<HeadScreens*>(p);HeadScreenEvent selected;selected.ui.command=UiCommand::Back;return screens.event(selected)==HeadScreenResult::Ok;};
    character.services.submit=[](void* p,UiCommand command,const std::array<char,65>& text){auto& screens=*static_cast<HeadScreens*>(p);HeadScreenEvent selected;selected.kind=HeadScreenEventKind::Text;selected.ui.command=command;selected.text=text;return screens.event(selected)==HeadScreenResult::Ok;};
}
HeadScreenResult HeadScreens::assertion(std::uint32_t line){
    painter.assertion_line=line;if(!services.assertion)return HeadScreenResult::MissingService;
    return services.assertion(services.context,line)?HeadScreenResult::Ok:HeadScreenResult::AssertionFailure;
}
void HeadScreens::clear_data() noexcept {menu_data.clear();menu_data.language=language;scroll_data.clear();}
HeadScreenResult HeadScreens::reset(){
    bind();if(!services.millis)return HeadScreenResult::MissingService;
    state.reset_time=services.millis(services.context);state.layout_marker=22;state.default_command=UiCommand::None;state.available=DisplayBounds;
    encoder.menu.enabled=false;menu.state.marquee.deadline=0;menu.state.marquee.active=false;
    encoder.paragraph.enabled=false;paragraph.state.deadline=0;
    character.reset();encoder.diagnostic_line=0;state.runtime_info_command=UiCommand::None;state.pause_reason=PauseReason::None;
    encoder.temperature.enabled=false;encoder.temperature.value=100;
    encoder.duration.enabled=false;encoder.duration.seconds=0;state.duration_session=SessionType::Unknown;
    encoder.strength.enabled=false;encoder.strength.value=5;
    for(auto& quantity:encoder.quantities)quantity.enabled=false;
    PAINT_TRY(slideshow.reset());
    encoder.combined.enabled=false;periodic=UiScreenTickState{};periodic.waiting.frame_count=4;
    state.waiting_first.fill(0);state.waiting_second.fill(0);state.title.fill(0);state.title_height=0;state.title_underline=true;state.title_enabled=false;
    display_clear(display);if(text_context&&display_text_bounds(*text_context,DisplayBounds)!=DisplayTextResult::Ok)return HeadScreenResult::RenderFailure;painter.dirty=true;return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::title(const char* value,bool underline){
    if(!value||!*value)SCREEN_TRY(assertion(0x1ed));
    if(display_rect_height(state.available)!=64||state.default_command!=UiCommand::None)SCREEN_TRY(assertion(0x1f0));
    copy(state.title,value);state.title_enabled=true;state.title_underline=underline;
    PAINT_TRY(painter.title(value,underline,state.title_height));state.available.top+=state.title_height;return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::show(const HeadScreenRequest& request){
    // Snapshot all borrowed strings before reset or any subsequent callback.
    if(request.body.count>body_storage.size())return HeadScreenResult::InvalidText;
    auto owned_body=body_storage;std::array<char,129> owned_title{};copy(owned_title,request.title);
    for(std::uint32_t i=0;i<request.body.count;++i){if(!request.body.lines||!request.body.lines[i])return HeadScreenResult::InvalidText;copy(owned_body[i],request.body.lines[i]);}
    body_storage=owned_body;body_count=request.body.count;for(std::uint32_t i=0;i<body_count;++i)body_lines[i]=body_storage[i].data();
    HeadMenuBody body=request.body;body.lines=body_lines.data();
    SCREEN_TRY(reset());state.requested_layout=request.screen.layout;
    const char* heading=owned_title.data();if(*heading)SCREEN_TRY(title(heading,true));
    switch(request.screen.layout){
    case 0:state.default_command=request.screen.command;periodic.runtime_state.enabled=true;periodic.runtime_state.deadline=services.millis(services.context)+500;PAINT_TRY(dynamic.runtime_state());break;
    case 1:state.runtime_info_command=request.screen.command;periodic.runtime_transient.enabled=true;PAINT_TRY(dynamic.runtime_info(state.available));periodic.runtime_transient.deadline=services.millis(services.context)+500;break;
    case 14:state.default_command=request.screen.command;periodic.level.enabled=true;PAINT_TRY(dynamic.level());break;
    case 16:SCREEN_TRY(pause(static_cast<PauseReason>(observed.runtime.pause_reason)));break;
    case 3:
        state.default_command=request.screen.command;
        if(body_count!=3)SCREEN_TRY(assertion(0x32b));
        if(body_count<3)return HeadScreenResult::InvalidText;
        PAINT_TRY(painter.text_block(state.available,body_lines.data(),1,DisplayFontId::Font6x8,DisplayTextAlignment::Center,0,2));
        PAINT_TRY(painter.text_block(state.available,body_lines.data()+1,1,DisplayFontId::Font9x11,DisplayTextAlignment::Center,1));
        PAINT_TRY(painter.text_block(state.available,body_lines.data()+2,1,DisplayFontId::Font6x8,DisplayTextAlignment::Center,2,2));break;
    case 6:{
        const auto command=request.custom_scroll?request.scroll_command:UiCommand::Exit;
        const char* button=request.custom_scroll?(request.scroll_button?request.scroll_button:""):"Exit";
        PARAGRAPH_RESULT(paragraph.initialize(state.available,request.scroll_alignment,command,button,false));
        periodic.paragraph_caret.enabled=true;periodic.paragraph_caret.deadline=paragraph.state.deadline;periodic.paragraph_caret.interval_ms=paragraph.state.interval_ms;break;
    }
    case 7:{
        auto converted=[&](double value){float result=static_cast<float>(value);if(observed.imperial)result=static_cast<float>(static_cast<double>(result)*1.8+32);return temperature_integer(result);};
        auto& widget=encoder.temperature;widget.enabled=true;widget.minimum=converted(observed.minimum_temperature);widget.maximum=converted(observed.maximum_temperature);
        const auto current=converted(observed.temperature);widget.value=current<widget.minimum?widget.minimum:current<widget.maximum?current:widget.maximum;state.metric=!observed.imperial;
        PAINT_TRY(painter.temperature(state.available,widget.value,state.metric));break;
    }
    case 8:CHARACTER_RESULT(character.initialize(state.available,request.screen.input_kind,request.screen.input_limit,request.screen.response));break;
    case 9:
        encoder.duration.enabled=true;encoder.duration.seconds=observed.duration;state.duration_session=observed.session;
        PAINT_TRY(painter.duration(state.available,encoder.duration.seconds));break;
    case 10:
        if(request.screen.strength<1||request.screen.strength>9)SCREEN_TRY(assertion(0x79f));
        encoder.strength.enabled=true;encoder.strength.value=static_cast<std::int32_t>(request.screen.strength);
        PAINT_TRY(painter.strength(state.available,request.screen.strength));break;
    case 11:{
        const auto value=request.screen.selection;if(value<1||value>6)SCREEN_TRY(assertion(0x4ba));
        state.valve=std::max<std::uint32_t>(1,std::min<std::uint32_t>(6,value));
        auto& selection=encoder.quantities[0];selection.enabled=true;selection.wrap=true;selection.count=6;selection.selected=static_cast<std::int32_t>(state.valve)-1;
        PAINT_TRY(painter.valve(state.available,state.valve));break;
    }
    case 12:QUANTITY_RESULT(quantity.initialize(state.available,static_cast<HeadQuantityId>(request.screen.quantity_id),request.screen.quantity,!observed.imperial));break;
    case 13:{
        if(!body_count)return HeadScreenResult::InvalidText;
        copy(state.waiting_first,body_lines[0]);if(request.counter){std::array<char,10> suffix{};::snprintf(suffix.data(),suffix.size()," %ds",static_cast<int>(request.counter));std::string text(state.waiting_first.data());text+=suffix.data();copy(state.waiting_first,text.c_str());}
        if(body_count==2)copy(state.waiting_second,body_lines[1]);
        periodic.waiting.enabled=true;periodic.waiting.animate=request.animate;periodic.waiting.frame=0;periodic.waiting.frame_count=4;
        if(request.animate)periodic.waiting.deadline=services.millis(services.context)+500;
        PAINT_TRY(painter.waiting(state.available,state.waiting_first.data(),state.waiting_second.data(),request.animate,0));break;
    }
    case 15:state.progress=request.screen.selection;state.progress_percentage=request.percentage;PAINT_TRY(painter.progress(state.available,request.screen.selection,request.percentage));break;
    case 17:
        SCREEN_TRY(reset());MENU_RESULT(menu.compose(state.available,heading,request.menu_rows,request.has_body?&body:nullptr,request.menu_vertical));
        if(*heading){copy(state.title,heading);state.title_enabled=true;state.title_underline=encoder.menu.visible_rows==1||request.has_body;state.title_height=state.title_underline?14:12;}break;
    case 18:{
        const auto* bitmap=ui_bitmap(request.icon);if(!bitmap)return HeadScreenResult::RenderFailure;
        if(display_bitmap(display,{static_cast<std::int32_t>((128-bitmap->width)/2),4},*bitmap,bitmap->bounds)!=DisplayResult::Ok)return HeadScreenResult::RenderFailure;
        state.available.top=static_cast<std::int32_t>(bitmap->height+4);
        MENU_RESULT(menu.compose(state.available,heading,request.menu_rows,nullptr,request.menu_vertical));
        if(*heading){state.title_underline=encoder.menu.visible_rows==1;state.title_height=state.title_underline?14:12;}break;
    }
    case 19:SCREEN_TRY(reset());SCREEN_TRY(paragraph_menu(heading));break;
    case 20:
        PARAGRAPH_RESULT(paragraph.initialize(state.available,DisplayTextAlignment::Center,UiCommand::None,"",true));
        MENU_RESULT(menu.compose(state.available,"",1,nullptr,request.menu_vertical));encoder.combined.enabled=true;encoder.combined.selected=0;
        encoder.combined.paragraph_count=static_cast<std::uint8_t>(scroll_data.count-encoder.paragraph.visible_lines);encoder.combined.menu_count=static_cast<std::uint8_t>(menu_data.count);
        periodic.combined=true;periodic.combined_caret.enabled=true;periodic.combined_caret.deadline=paragraph.state.deadline;periodic.combined_caret.interval_ms=paragraph.state.interval_ms;break;
    case 21:
        if(!request.has_body){SCREEN_TRY(assertion(0x370));return HeadScreenResult::InvalidText;}
        if(request.screen.command!=UiCommand::None)state.default_command=request.screen.command;
        PAINT_TRY(painter.text_block(state.available,body.lines,body.count,DisplayFontId::Font6x8,body.horizontal,body.vertical));break;
    case 2:
        if(menu_data.count){SCREEN_TRY(reset());MENU_RESULT(menu.compose(state.available,heading,1,nullptr,2));state.available.bottom-=2;if(*heading){copy(state.title,heading);state.title_enabled=true;state.title_underline=true;state.title_height=14;}}
        state.hardware_extra=request.extra_info;state.hardware_metric=!observed.imperial;periodic.notification.enabled=true;PAINT_TRY(hardware.draw(state.available,state.hardware_metric,state.hardware_extra));periodic.notification.deadline=services.millis(services.context)+500;break;
    case 4:PAINT_TRY(slideshow.initialize(request.screen.argument,request.screen.command,observed.imperial,observed.show_detail,observed.static_cycle));break;
    case 5:
        if(!menu_data.count)SCREEN_TRY(assertion(0x308));
        state.default_command=request.screen.command;
        if(menu_data.count){SCREEN_TRY(reset());MENU_RESULT(menu.compose(state.available,heading,1,nullptr,2));state.available.bottom-=2;if(*heading){copy(state.title,heading);state.title_enabled=true;state.title_underline=true;state.title_height=14;}}
        state.still_imperial=observed.imperial;periodic.still_transient.enabled=true;periodic.still_transient.deadline=0;break;
    default:break; // The actual OEM invalid-layout branch clears and logs.
    }
    painter.dirty=true;return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::paragraph_menu(const char* heading){
    if(heading&&*heading)SCREEN_TRY(title(heading,true));
    DisplayRect button{state.available.left+10,state.available.bottom-8,state.available.right-10,state.available.bottom};
    MENU_RESULT(menu.initialize(button,1));state.available.bottom=button.top-2;
    const auto height=display_rect_height(state.available),count=scroll_data.count;
    const std::uint32_t spacing=count*8<=height?(height-count*8)/(count+1):0;
    const std::uint32_t text_height=count*(8+spacing)-spacing;
    if(!count||text_height>height)return HeadScreenResult::InvalidState;
    DisplayRect bounds=state.available;bounds.top+=static_cast<std::int32_t>((height-text_height)/2);bounds.bottom=bounds.top+static_cast<std::int32_t>(text_height)-1;
    DisplayTextContext context;const auto* font=display_font(DisplayFontId::Font6x8);
    if(!font||display_text_font(context,*font,spacing)!=DisplayTextResult::Ok||display_text_bounds(context,bounds)!=DisplayTextResult::Ok)return HeadScreenResult::RenderFailure;
    for(std::uint32_t i=0;i<count;++i){if(!scroll_data.lines[i][0]){SCREEN_TRY(assertion(0x3bf));continue;}DisplayTextOptions options;options.row=static_cast<std::int32_t>(i);options.alignment=DisplayTextAlignment::Center;
        if(display_text(display,context,scroll_data.lines[i].data(),scroll_data.lines[i].size(),options)!=DisplayTextResult::Ok)return HeadScreenResult::RenderFailure;}
    painter.dirty=true;return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::menu_screen(const char* heading,std::uint32_t rows,std::uint32_t vertical){
    if(!menu_data.count)SCREEN_TRY(assertion(0x7f));
    HeadScreenRequest request;request.title=heading;request.screen.layout=17;
    request.menu_rows=static_cast<std::uint8_t>(rows?rows:menu_data.count);request.menu_vertical=vertical;return show(request);
}
HeadScreenResult HeadScreens::instruction_menu(ScreenId id,const char* heading){
    const char* source=nullptr;if(ui_screen_text(id,language,source)!=UiScreenTextResult::Ok)return HeadScreenResult::InvalidText;
    HeadWrappedText wrapped;if(!head_wrap_text(wrapped,source))return HeadScreenResult::InvalidText;
    if(!menu_data.count)SCREEN_TRY(assertion(0x97));
    HeadScreenRequest request;request.title=heading;request.menu_rows=1;
    if(wrapped.count<5){std::array<const char*,12> lines{};for(std::uint32_t i=0;i<wrapped.count;++i)lines[i]=wrapped.lines[i].data();request.screen.layout=17;request.has_body=true;request.body.lines=lines.data();request.body.count=wrapped.count;request.menu_vertical=2;return show(request);}
    scroll_data.clear();for(std::uint32_t i=0;i<wrapped.count;++i)if(!scroll_data.append(wrapped.lines[i].data()))return HeadScreenResult::InvalidText;
    request.screen.layout=20;request.menu_vertical=2;return show(request);
}
HeadScreenResult HeadScreens::waiting(const char* heading,const char* detail,bool animate,std::uint16_t counter){
    const char* lines[]={ui_localized_text(UiTextCategory::TextCommon,14,language),detail};
    HeadScreenRequest request;request.title=heading;request.screen.layout=13;request.has_body=true;request.body.lines=lines;request.body.count=detail&&*detail?2:1;request.animate=animate;request.counter=static_cast<std::uint8_t>(counter);return show(request);
}
HeadScreenResult HeadScreens::registration(const char* token){
    const char* lines[]={ui_localized_text(UiTextCategory::TextCommon,8,language),token,ui_localized_text(UiTextCategory::TextCommon,9,language)};
    HeadScreenRequest request;request.title=ui_localized_text(UiTextCategory::TextTitle,36,language);request.screen.layout=3;request.has_body=true;request.body.lines=lines;request.body.count=3;return show(request);
}
namespace { const char* const DiagnosticParagraphs[]={
    "just a normal line",
    "this line should break into two parts",
    "this line should break into three lines",
    "this line should break into four lines 111 1111 1111 1111 1111",
    "Here is a long paragraph which will just fit inside of an entire screen with the small font testing a full load",
    "Here is a long paragraph which will not fit inside of an entire screen with the small font testing a full load with truncation ending the paragraph to fit",
    "Tabs\011and\011linebreaks\012treated\012as\012spaces",
    "test line breaks and spacing with ascii characters 1/2",
    "t\303\251st line br\303\251\303\242ks and sp\303\242cing with \303\242scii ch\303\242ract\303\251rs 2/2",
    "\303\242\303\251 begin and end ascii \303\242\303\251",
    "nolinespacesforoverasingleline",
    "normal nolinespacesforoverasingleline",
    "normal nolinespacesforoverasingleline normal",
    "nolin\303\251sp\303\242c\303\251sforover\303\242singleline",
    "normal nolin\303\251sp\303\242c\303\251sforover\303\242singleline normal",
    "running spaces         running spaces",
    "trailing spaces                                     ",
    "test using a larger font breaking 4 lines",
    "in a box",
    "two lines in a box",
    "filling out the entire box with text to test view overflow in a boxed in area",
    "f\303\255ll\303\255ng out th\303\251 \303\251nt\303\255r\303\251 box w\303\255th t\303\251xt to t\303\251st v\303\255\303\251w ov\303\251rflow \303\255n \303\242 box\303\251d in \303\242r\303\251\303\242",
}; }
HeadScreenResult HeadScreens::diagnostic_paragraph(std::int32_t delta){
    const bool first=encoder.diagnostic_line==0;const auto index=first?0:std::max<std::int32_t>(0,std::min<std::int32_t>(21,static_cast<std::int32_t>(encoder.diagnostic_line)-1+delta));SCREEN_TRY(reset());state.default_command=UiCommand::Exit;
    const bool box=index>17,large=index==17;HeadWrappedText wrapped;if(!head_wrap_text(wrapped,DiagnosticParagraphs[index],box?68:4,large?9:6,box?6:12))return HeadScreenResult::InvalidText;
    std::array<const char*,12> lines{};for(std::uint32_t i=0;i<wrapped.count;++i)lines[i]=wrapped.lines[i].data();const DisplayRect rect=box?DisplayRect{1,1,64,64}:state.available;
    if(box&&display_rectangle(display,rect,15,false)!=DisplayResult::Ok)return HeadScreenResult::RenderFailure;
    PAINT_TRY(painter.text_block(rect,lines.data(),wrapped.count,large?DisplayFontId::Font9x11:DisplayFontId::Font6x8,DisplayTextAlignment::Center,1));encoder.diagnostic_line=static_cast<std::uint8_t>(index+1);painter.dirty=true;return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::pause(PauseReason reason){
    bind();if(!services.millis)return HeadScreenResult::MissingService;
    if(menu_data.count){MENU_RESULT(menu.compose(state.available,state.title.data(),1,nullptr,2));state.available.bottom-=2;}
    state.pause_reason=reason;periodic.pause.enabled=true;periodic.pause.deadline=services.millis(services.context)+500;
    PAINT_TRY(dynamic.pause(state.available,reason));return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::progress(std::uint32_t value){
    bind();if(value>100)return assertion(0x2e7);
    if(value!=state.progress){state.progress=value;PAINT_TRY(painter.progress(state.available,static_cast<std::uint16_t>(value),state.progress_percentage));}
    painter.dirty=true;return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::home(){
    clear_data();if(observed.service_mode)menu_data.append(static_cast<UiCommand>(81));
    if(!observed.offline){menu_data.append(static_cast<UiCommand>(5));menu_data.append(static_cast<UiCommand>(7));menu_data.append(static_cast<UiCommand>(20));}
    for(auto id:{82,78,31,41,42})menu_data.append(static_cast<UiCommand>(id));
    if(observed.service_mode)menu_data.append(static_cast<UiCommand>(43));
    menu_data.append(static_cast<UiCommand>(45));
    if(observed.service_mode||observed.offline)return menu_screen(ui_localized_text(UiTextCategory::TextTitle,observed.service_mode?40:41,language));
    HeadScreenRequest request;request.screen.layout=18;request.menu_rows=3;request.menu_vertical=1;request.icon=UiBitmapId::LogoSmall;return show(request);
}
HeadScreenResult HeadScreens::event(const HeadScreenEvent& value){if(!services.post_event)return HeadScreenResult::MissingService;return services.post_event(services.context,value)?HeadScreenResult::Ok:HeadScreenResult::ServiceFailure;}
HeadScreenResult HeadScreens::button(){
    bind();
    if(encoder.menu.enabled){if(encoder.menu.selected>=menu_data.count)return HeadScreenResult::InvalidState;HeadScreenEvent selected;selected.ui=menu_data.items[encoder.menu.selected].event;SCREEN_TRY(event(selected));encoder.menu.enabled=false;menu.state.marquee.active=false;menu.state.marquee.deadline=0;}
    if(encoder.combined.enabled)return HeadScreenResult::Ok;
    if(periodic.runtime_transient.enabled&&state.runtime_info_command!=UiCommand::None){HeadScreenEvent selected;selected.ui.command=state.runtime_info_command;SCREEN_TRY(event(selected));periodic.runtime_transient.enabled=false;periodic.runtime_transient.deadline=0;}
    if(encoder.paragraph.enabled&&paragraph.state.command!=UiCommand::None){encoder.paragraph.enabled=false;periodic.paragraph_caret.enabled=false;HeadScreenEvent selected;selected.ui.command=paragraph.state.command;SCREEN_TRY(event(selected));}
    if(encoder.character.enabled)CHARACTER_RESULT(character.button());
    if(encoder.slideshow.enabled)PAINT_TRY(slideshow.button());
    if(encoder.temperature.enabled){HeadScreenEvent selected;selected.kind=HeadScreenEventKind::Temperature;selected.temperature=static_cast<float>(encoder.temperature.value);if(!state.metric)selected.temperature=static_cast<float>(static_cast<double>(selected.temperature-32.0f)/1.8);SCREEN_TRY(event(selected));PAINT_TRY(painter.temperature(state.available,encoder.temperature.value,state.metric));painter.dirty=true;}
    if(encoder.duration.enabled){HeadScreenEvent selected;selected.kind=HeadScreenEventKind::Duration;selected.value=encoder.duration.seconds;SCREEN_TRY(event(selected));PAINT_TRY(painter.duration(state.available,selected.value));}
    if(encoder.strength.enabled){HeadScreenEvent selected;selected.kind=HeadScreenEventKind::Strength;selected.value=static_cast<std::uint32_t>(encoder.strength.value);SCREEN_TRY(event(selected));PAINT_TRY(painter.strength(state.available,selected.value));painter.dirty=true;}
    if(encoder.quantities[0].enabled){HeadScreenEvent selected;selected.kind=HeadScreenEventKind::Location;selected.value=state.valve;SCREEN_TRY(event(selected));}
    if(encoder.quantities[1].enabled)QUANTITY_RESULT(quantity.button());
    if(state.default_command!=UiCommand::None){HeadScreenEvent selected;selected.ui.command=state.default_command;SCREEN_TRY(event(selected));}
    return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::select_combined_paragraph(std::uint32_t selected){
    selected&=255;
    if(!encoder.paragraph.enabled||scroll_data.count<selected+static_cast<std::uint32_t>(encoder.paragraph.visible_lines))return HeadScreenResult::Ok;
    encoder.paragraph.offset=static_cast<std::int32_t>(selected);PARAGRAPH_RESULT(paragraph.draw());return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::select_combined_menu(std::uint32_t selected){
    selected&=255;
    if(!encoder.menu.enabled||!encoder.menu.visible_rows||selected>=menu_data.count||selected==encoder.menu.selected)return HeadScreenResult::Ok;
    encoder.menu.selected=selected;encoder.menu.scroll_offset=selected-encoder.menu.selection_row;MENU_RESULT(menu.draw());painter.dirty=true;return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::encoder_effect(HeadEncoderEffect effect,std::uint32_t first,std::uint32_t second){
    bind();switch(effect){
    case HeadEncoderEffect::MenuDraw:MENU_RESULT(menu.draw());painter.dirty=true;break;
    case HeadEncoderEffect::SlideshowSelect:PAINT_TRY(slideshow.select(first));break;
    case HeadEncoderEffect::ParagraphDraw:PARAGRAPH_RESULT(paragraph.draw());break;
    case HeadEncoderEffect::CharacterDraw:CHARACTER_RESULT(character.draw());painter.dirty=true;break;
    case HeadEncoderEffect::TemperatureDraw:PAINT_TRY(painter.temperature(state.available,encoder.temperature.value,state.metric));painter.dirty=true;break;
    case HeadEncoderEffect::DurationDraw:PAINT_TRY(painter.duration(state.available,encoder.duration.seconds));break;
    case HeadEncoderEffect::StrengthDraw:PAINT_TRY(painter.strength(state.available,static_cast<std::uint32_t>(encoder.strength.value)));painter.dirty=true;break;
    case HeadEncoderEffect::QuantityChanged:if(first){QUANTITY_RESULT(quantity.changed());break;}state.valve=second+1;PAINT_TRY(painter.valve(state.available,state.valve));break;
    case HeadEncoderEffect::DiagnosticParagraph:return diagnostic_paragraph(static_cast<std::int32_t>(first));
    case HeadEncoderEffect::CombinedParagraphSelect:return select_combined_paragraph(first);
    case HeadEncoderEffect::CombinedMenuSelect:return select_combined_menu(first);
    default:return HeadScreenResult::UnsupportedLayout;
    }
    return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::tick_effect(UiScreenTickEffect effect,std::uint32_t first,std::uint32_t){
    bind();switch(effect){
    case UiScreenTickEffect::RuntimeStateDraw:PAINT_TRY(dynamic.runtime_state());break;
    case UiScreenTickEffect::RuntimeTransientDraw:PAINT_TRY(dynamic.runtime_info(state.available));periodic.runtime_transient.deadline=services.millis(services.context)+500;break;
    case UiScreenTickEffect::PauseDraw:periodic.pause.deadline=first+500;PAINT_TRY(dynamic.pause(state.available,state.pause_reason));break;
    case UiScreenTickEffect::LevelDraw:PAINT_TRY(dynamic.level());break;
    case UiScreenTickEffect::MenuTextScrollTick:MENU_RESULT(menu.tick(first));break;
    case UiScreenTickEffect::SlideshowTick:PAINT_TRY(slideshow.tick(first));break;
    case UiScreenTickEffect::StillTransientDraw:PAINT_TRY(still.draw(state.available,state.still_imperial));break;
    case UiScreenTickEffect::NotificationDraw:PAINT_TRY(hardware.draw(state.available,state.hardware_metric,state.hardware_extra));periodic.notification.deadline=services.millis(services.context)+500;break;
    case UiScreenTickEffect::WaitingDraw:PAINT_TRY(painter.waiting(state.available,state.waiting_first.data(),state.waiting_second.data(),periodic.waiting.animate,periodic.waiting.frame));break;
    case UiScreenTickEffect::ScrollCaretDraw:PARAGRAPH_RESULT(paragraph.caret());break;
    default:return HeadScreenResult::UnsupportedLayout;
    }
    return HeadScreenResult::Ok;
}
HeadScreenResult HeadScreens::ui_action(const HeadUiAction& action){
    bind();switch(action.effect){
    case HeadUiEffect::ResetScreen:clear_data();return HeadScreenResult::Ok;
    case HeadUiEffect::AppendCommand:case HeadUiEffect::AppendCommandText:return menu_data.append(static_cast<UiCommand>(action.first),action.text)?HeadScreenResult::Ok:HeadScreenResult::InvalidState;
    case HeadUiEffect::AppendCommandFront:return menu_data.prepend(static_cast<UiCommand>(action.first),action.text)?HeadScreenResult::Ok:HeadScreenResult::InvalidState;
    case HeadUiEffect::Menu:return menu_screen(action.title,action.first,action.second);
    case HeadUiEffect::InstructionMenu:return instruction_menu(static_cast<ScreenId>(action.first),ui_localized_text(UiTextCategory::TextTitle,action.second,language));
    case HeadUiEffect::Screen:{HeadScreenRequest request;request.title=action.title;request.screen=action.screen;return show(request);}
    case HeadUiEffect::WaitingMessage:return waiting(action.title,action.text,action.first!=0,static_cast<std::uint16_t>(action.second));
    case HeadUiEffect::CoffeeMenu:clear_data();for(auto id:{32,33,34,35,1})menu_data.append(static_cast<UiCommand>(id));return menu_screen(ui_localized_text(UiTextCategory::TextTitle,7,language));
    default:return HeadScreenResult::NotHandled;
    }
}
HeadScreenResult HeadScreens::core_action(const HeadCoreAction& action){
    bind();switch(action.effect){
    case HeadCoreEffect::ResetScreen:clear_data();return HeadScreenResult::Ok;
    case HeadCoreEffect::ResetUi:return reset();
    case HeadCoreEffect::ClearDisplay:display_clear(display);painter.dirty=true;return HeadScreenResult::Ok;
    case HeadCoreEffect::AppendCommand:case HeadCoreEffect::AppendCommandText:return menu_data.append(static_cast<UiCommand>(action.first),action.text)?HeadScreenResult::Ok:HeadScreenResult::InvalidState;
    case HeadCoreEffect::AppendLine:return scroll_data.append(action.text)?HeadScreenResult::Ok:HeadScreenResult::InvalidText;
    case HeadCoreEffect::InstructionMenu:return instruction_menu(static_cast<ScreenId>(action.first),ui_localized_text(UiTextCategory::TextTitle,action.second,language));
    case HeadCoreEffect::WaitingMessage:return waiting(action.title,action.text,action.first!=0,static_cast<std::uint16_t>(action.second));
    case HeadCoreEffect::RegistrationScreen:return registration(action.text);
    case HeadCoreEffect::ProgressScreen:{HeadScreenRequest request;request.title=action.title;request.screen.layout=15;request.screen.selection=static_cast<std::uint16_t>(action.first);request.percentage=action.second;return show(request);}
    case HeadCoreEffect::HomeScreen:return home();
    case HeadCoreEffect::ErrorScrollScreen:{HeadScreenRequest request;request.title=action.title;request.screen.layout=6;return show(request);}
    case HeadCoreEffect::BootLogo:{display_clear(display);const auto* bitmap=ui_bitmap(UiBitmapId::LogoLarge);if(!bitmap)return HeadScreenResult::RenderFailure;
        if(display_slide_aligned(display,display_io,display_delay,*bitmap,2,1,1,1,0)!=DisplayEffectResult::Ok)return HeadScreenResult::RenderFailure;
        if(!display_delay.milliseconds)return HeadScreenResult::MissingService;
        display_delay.milliseconds(display_delay.context,200);return HeadScreenResult::Ok;}
    default:return HeadScreenResult::NotHandled;
    }
}
#undef SCREEN_TRY
#undef PAINT_TRY
#undef MENU_RESULT
#undef PARAGRAPH_RESULT
#undef QUANTITY_RESULT
#undef CHARACTER_RESULT
}} // namespace zseries::oem
