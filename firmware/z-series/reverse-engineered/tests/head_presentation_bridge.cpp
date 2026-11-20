#include "zseries/head_hardware.hpp"
#include "zseries/head_still_display.hpp"
#include <cstring>
using namespace zseries::oem;
namespace {struct Context{std::uint32_t count=7,line=0,clock=777;};bool assertion(void* p,std::uint32_t line){auto& c=*static_cast<Context*>(p);c.line=line;++c.count;c.clock+=200;return true;}}
extern "C" unsigned head_presentation_test(const std::uint32_t* args,const std::uint32_t* words,const double* values,std::uint8_t* pixels,std::uint32_t* out){
    DisplayState display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<std::uint8_t>((i*73+17)^(i>>3));HeadWidgetPainter painter(display);HeadDynamicObserved o;Context context;painter.services.context=&context;painter.services.assertion=assertion;painter.language=static_cast<LanguageType>(args[2]);
    o.runtime.still.id=words[0];o.runtime.t1_drop_watch.enabled=words[1];o.runtime.t1_drop_watch.triggered=words[2];o.still_samples.current.temperature[0]=values[0];o.still_samples.current.temperature[1]=values[1];o.runtime.t1_drop_watch.maximum=values[2];
    const DisplayRect rect{static_cast<std::int32_t>(args[4]),static_cast<std::int32_t>(args[5]),static_cast<std::int32_t>(args[6]),static_cast<std::int32_t>(args[7])};HeadWidgetResult result;
    if(args[0]==0){HeadHardwarePainter view(painter,o);result=view.draw(rect,!args[1],args[3]);}else{HeadStillDisplay view(painter,o);result=view.draw(rect,args[1]);}
    std::memcpy(pixels,display.pixels.data(),4096);out[0]=painter.dirty;out[1]=context.line;out[2]=context.count;out[3]=context.clock;return static_cast<unsigned>(result);
}
