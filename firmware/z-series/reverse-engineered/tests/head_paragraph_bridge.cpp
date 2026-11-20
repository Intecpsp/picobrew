#include "zseries/head_paragraph.hpp"
#include <cstring>
using namespace zseries::oem;
using Word=std::uint32_t;
namespace {Word clock(void*){return 777;}}
extern "C" unsigned head_paragraph_test(const Word* args,const std::int32_t* bounds,const char* button,
 const char* const* lines,Word count,unsigned char* pixels,Word* out){
    DisplayState display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<unsigned char>((i*73+17)^(i>>3));
    HeadWidgetPainter painter(display);HeadScrollData data;for(Word i=0;i<count;++i)data.append(lines[i]);
    EncoderParagraph selection;HeadParagraphRenderer renderer(painter,data,selection);renderer.services.millis=clock;
    DisplayRect rect{bounds[0],bounds[1],bounds[2],bounds[3]};auto result=renderer.initialize(rect,static_cast<DisplayTextAlignment>(args[0]),static_cast<UiCommand>(args[1]),button,args[2]);
    if(result!=HeadParagraphResult::Ok)return static_cast<unsigned>(result);
    if(args[3]!=0xffffffffu){selection.offset=args[3];result=renderer.draw();if(result!=HeadParagraphResult::Ok)return static_cast<unsigned>(result);}
    for(Word i=0;i<args[4];++i){result=renderer.caret();if(result!=HeadParagraphResult::Ok)return static_cast<unsigned>(result);}
    const auto& state=renderer.state;Word index=0;
    for(const auto& r:{state.rect,state.up,state.down}){out[index++]=r.left;out[index++]=r.top;out[index++]=r.right;out[index++]=r.bottom;}
    Word values[]={selection.enabled,static_cast<Word>(selection.offset),static_cast<Word>(selection.visible_lines),data.count,static_cast<Word>(state.alignment),static_cast<Word>(state.command),state.text_offset,state.deadline,state.caret_visible,painter.dirty,0,7};
    std::memcpy(out+index,values,sizeof values);std::memcpy(pixels,display.pixels.data(),display.pixels.size());return 0;
}
