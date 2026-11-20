#include "zseries/head_quantity.hpp"
#include <cstring>
using namespace zseries::oem;
namespace {struct Context{std::uint32_t count=7,line=0;};bool assertion(void* p,std::uint32_t line){auto& c=*static_cast<Context*>(p);c.line=line;++c.count;return true;}}
extern "C" unsigned head_quantity_test(const std::uint32_t* args,double value,std::uint8_t* pixels,std::uint32_t* out,double* scalars,char* text){
    const Quantity input{value,static_cast<Unit>(args[2])};if(args[0])return head_quantity_text(text,32,input,args[3]!=0,static_cast<std::int32_t>(args[4]))?0:99;
    DisplayState display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<std::uint8_t>((i*73+17)^(i>>3));
    EncoderQuantity selected;HeadWidgetPainter painter(display);HeadQuantity quantity(painter,selected);Context context;painter.services.context=&context;painter.services.assertion=assertion;
    DisplayRect rect{static_cast<std::int32_t>(args[5]),static_cast<std::int32_t>(args[6]),static_cast<std::int32_t>(args[7]),static_cast<std::int32_t>(args[8])};
    const auto result=quantity.initialize(rect,static_cast<HeadQuantityId>(args[1]),input,args[3]!=0);
    std::memcpy(pixels,display.pixels.data(),4096);out[0]=quantity.state.metric;out[1]=static_cast<std::uint32_t>(quantity.state.unit);out[2]=static_cast<std::uint32_t>(quantity.state.id);out[3]=selected.enabled;out[4]=selected.wrap;out[5]=static_cast<std::uint32_t>(selected.selected);out[6]=static_cast<std::uint32_t>(selected.count);out[7]=painter.dirty;out[8]=context.line;out[9]=context.count;scalars[0]=quantity.state.minimum;scalars[1]=quantity.state.step;
    return static_cast<unsigned>(result);
}
