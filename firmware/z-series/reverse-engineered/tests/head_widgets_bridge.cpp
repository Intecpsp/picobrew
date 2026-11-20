#include "zseries/head_widgets.hpp"
#include <cstring>
using namespace zseries::oem;
using Word=std::uint32_t;
namespace {struct Context{Word count;};bool assertion(void* p,Word){++static_cast<Context*>(p)->count;return true;}}
extern "C" unsigned head_widgets_test(const Word* args,const std::int32_t* bounds,const char* first,const char* second,const char* const* lines,Word count,unsigned char* pixels,Word* result){
    DisplayState display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<unsigned char>((i*73+args[8])^(i>>3));
    HeadWidgetPainter painter(display);painter.language=static_cast<LanguageType>(args[3]);Context context{args[9]};painter.services.context=&context;painter.services.assertion=assertion;
    DisplayRect rect{bounds[0],bounds[1],bounds[2],bounds[3]};HeadWidgetResult status=HeadWidgetResult::Ok;std::uint8_t height=0;
    switch(args[0]){
    case 0:status=painter.title(first,args[2],height);break;
    case 1:status=painter.progress(rect,args[1],args[2]);break;
    case 2:{std::int32_t value;std::memcpy(&value,args+1,4);status=painter.temperature(rect,value,args[2]);break;}
    case 3:status=painter.duration(rect,args[1]);break;
    case 4:status=painter.strength(rect,args[1]);break;
    case 5:status=painter.waiting(rect,first,second,args[2],args[1]);break;
    case 6:status=painter.valve(rect,args[1]);break;
    case 7:status=painter.text_block(rect,lines,count,static_cast<DisplayFontId>(args[4]),static_cast<DisplayTextAlignment>(args[5]),args[6],args[7]);break;
    default:return 99;
    }
    std::memcpy(pixels,display.pixels.data(),display.pixels.size());result[0]=painter.dirty;result[1]=painter.assertion_line;result[2]=context.count;result[3]=height;return static_cast<unsigned>(status);
}
