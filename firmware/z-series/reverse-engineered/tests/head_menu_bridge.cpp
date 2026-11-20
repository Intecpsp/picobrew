#include "zseries/head_menu.hpp"
#include <cstring>
using namespace zseries::oem;
using Word=std::uint32_t;
namespace {
struct Context {Word count=7;};
bool assertion(void* p,Word){++static_cast<Context*>(p)->count;return true;}
Word clock(void*){return 777;}
}
extern "C" unsigned head_menu_test(const Word* args,const std::int32_t* bounds,const char* title,
 const char* const* labels,Word count,const char* const* lines,Word line_count,
 const Word* ticks,Word tick_count,unsigned char* pixels,unsigned char* marquee_pixels,Word* out){
    DisplayState display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<unsigned char>((i*73+17)^(i>>3));
    HeadWidgetPainter painter(display);Context context;painter.services.context=&context;painter.services.assertion=assertion;
    HeadMenuData data;for(Word i=0;i<count;++i)data.append(static_cast<UiCommand>(i==0&&args[3]?1:10+i),labels[i]);
    EncoderMenu selection;HeadMenuRenderer renderer(painter,data,selection);renderer.services.millis=clock;
    DisplayRect available{bounds[0],bounds[1],bounds[2],bounds[3]};HeadMenuBody body;body.lines=lines;body.count=line_count;body.horizontal=static_cast<DisplayTextAlignment>(args[6]);body.vertical=args[7];
    auto result=args[0]==1?renderer.initialize(available,args[1]):renderer.compose(available,title,args[1],args[5]?&body:nullptr,args[2]);
    if(result!=HeadMenuResult::Ok)return static_cast<unsigned>(result);
    if(args[4]!=0xffffffffu){selection.selected=args[4];selection.scroll_offset=selection.selected-selection.selection_row;result=renderer.draw();if(result!=HeadMenuResult::Ok)return static_cast<unsigned>(result);}
    for(Word i=0;i<tick_count;++i){painter.dirty=false;result=renderer.tick(ticks[i]);if(result!=HeadMenuResult::Ok)return static_cast<unsigned>(result);}
    const auto& state=renderer.state;const auto& marquee=state.marquee;
    Word values[]={static_cast<Word>(available.left),static_cast<Word>(available.top),static_cast<Word>(available.right),static_cast<Word>(available.bottom),
      selection.enabled,selection.selected,selection.count,selection.visible_rows,selection.selection_row,selection.scroll_offset,
      static_cast<Word>(state.rect.left),static_cast<Word>(state.rect.top),static_cast<Word>(state.rect.right),static_cast<Word>(state.rect.bottom),state.mode,state.characters,state.last_visible,state.gradient,state.gradient_steps,
      static_cast<Word>(marquee.point.x),static_cast<Word>(marquee.point.y),marquee.width,marquee.full_width,marquee.source_x,marquee.deadline,marquee.active,painter.dirty,painter.assertion_line,context.count};
    std::memcpy(out,values,sizeof values);std::memcpy(pixels,display.pixels.data(),display.pixels.size());std::memcpy(marquee_pixels,marquee.pixels.data(),marquee.pixels.size());return 0;
}
extern "C" unsigned head_menu_ownership_and_guards(){
    HeadMenuData data;HeadUiEvent event;event.flags=4;event.recipe.id=1234;
    if(!data.append(event,"Selected recipe"))return 1;
    event.recipe.id=999;
    if(!data.prepend(UiCommand::Back)||data.items[1].event.recipe.id!=1234||data.items[1].event.flags!=4)return 2;
    DisplayState display;HeadWidgetPainter painter(display);EncoderMenu selection;HeadMenuRenderer renderer(painter,data,selection);
    data.clear();Context context;painter.services.context=&context;painter.services.assertion=assertion;
    if(renderer.initialize({10,54,118,63},1)!=HeadMenuResult::InvalidState||context.count!=9||painter.assertion_line!=0x126)return 3;
    data.append(UiCommand::Back,"A long label needs the clock service");
    if(renderer.initialize({10,54,118,63},1)!=HeadMenuResult::MissingService)return 4;
    return 0;
}
