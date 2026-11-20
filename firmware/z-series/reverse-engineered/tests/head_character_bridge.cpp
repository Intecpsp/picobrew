#include "zseries/head_character.hpp"
#include <cstring>
using namespace zseries::oem;
namespace {
struct Fixture {std::uint32_t count=7,line=0,event=0,command=0;std::array<char,65> text{};};
bool assertion(void* p,std::uint32_t line){auto& f=*static_cast<Fixture*>(p);f.line=line;++f.count;return true;}
bool cancel(void* p){auto& f=*static_cast<Fixture*>(p);f.event=1;f.command=1;return true;}
bool submit(void* p,UiCommand command,const std::array<char,65>& text){auto& f=*static_cast<Fixture*>(p);f.event=2;f.command=static_cast<std::uint32_t>(command);f.text=text;return true;}
}
extern "C" unsigned head_character_test(const std::uint32_t* args,const char* text,std::uint8_t* pixels,std::uint32_t* out,char* output,char* event){
    DisplayState display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<std::uint8_t>((i*73+17)^(i>>3));
    EncoderCharacter selected;HeadWidgetPainter painter(display);HeadCharacter character(painter,selected);Fixture fixture;
    painter.language=static_cast<LanguageType>(args[2]);painter.services.context=&fixture;painter.services.assertion=assertion;
    character.services.context=&fixture;character.services.cancel=cancel;character.services.submit=submit;
    auto& state=character.state;state.kind=args[1];selected.enabled=args[3]!=0;state.visible=args[4];selected.selected=static_cast<std::int32_t>(args[5]);state.limit=args[6];state.response=static_cast<UiCommand>(args[7]);state.truncated=args[8]!=0;
    state.rect={static_cast<std::int32_t>(args[9]),static_cast<std::int32_t>(args[10]),static_cast<std::int32_t>(args[11]),static_cast<std::int32_t>(args[12])};
    state.character_count=state.kind==0?10:state.kind==1?16:94;selected.count=state.character_count+(state.kind==2?4:3);
    state.main_font=state.kind==2?DisplayFontId::Font6x8:DisplayFontId::Font17x20;state.secondary_font=state.kind==2?DisplayFontId::Font6x8:DisplayFontId::Font9x11;
    std::strncpy(state.text.data(),text,64);state.length=static_cast<std::uint32_t>(std::strlen(state.text.data()));
    const auto result=args[0]==0?character.draw():character.button();
    std::memcpy(pixels,display.pixels.data(),4096);std::memcpy(output,state.text.data(),65);std::memcpy(event,fixture.text.data(),65);
    out[0]=selected.enabled;out[1]=static_cast<std::uint32_t>(selected.selected);out[2]=state.length;out[3]=state.truncated;out[4]=painter.dirty;out[5]=fixture.line;out[6]=fixture.count;out[7]=fixture.event;out[8]=fixture.command;
    return static_cast<unsigned>(result);
}
