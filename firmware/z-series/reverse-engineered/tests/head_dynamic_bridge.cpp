#include "zseries/head_dynamic.hpp"
#include <cstring>
using namespace zseries::oem;
namespace {struct Context{std::uint32_t count=7,line=0;};bool assertion(void* p,std::uint32_t line){auto& c=*static_cast<Context*>(p);c.line=line;++c.count;return true;}}
extern "C" unsigned head_dynamic_test(const std::uint32_t* args,const std::uint32_t* words,const double* values,const char* name,const char* step,const char* substep,const char* error,std::uint8_t* pixels,std::uint32_t* out,char* text){
    DisplayState display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<std::uint8_t>((i*73+17)^(i>>3));HeadWidgetPainter painter(display);HeadDynamicObserved o;Context context;painter.services.context=&context;painter.services.assertion=assertion;painter.language=static_cast<LanguageType>(args[2]);o.imperial=args[1];
    o.core.session_type=static_cast<decltype(o.core.session_type)>(words[0]);
    o.heater.started=static_cast<decltype(o.heater.started)>(words[1]);
    o.msp.status_byte_3=static_cast<decltype(o.msp.status_byte_3)>(words[2]);
    o.msp.valve_position=static_cast<decltype(o.msp.valve_position)>(words[3]);
    o.msp.moving_valve=static_cast<decltype(o.msp.moving_valve)>(words[4]);
    o.msp.pumps=static_cast<decltype(o.msp.pumps)>(words[5]);
    o.msp.fan=static_cast<decltype(o.msp.fan)>(words[6]);
    o.msp.errors=static_cast<decltype(o.msp.errors)>(words[7]);
    o.safety.filter_missing=static_cast<decltype(o.safety.filter_missing)>(words[8]);
    o.runtime.id=static_cast<decltype(o.runtime.id)>(words[9]);
    o.runtime.elapsed_seconds=static_cast<decltype(o.runtime.elapsed_seconds)>(words[10]);
    o.runtime.instruction_left_seconds=static_cast<decltype(o.runtime.instruction_left_seconds)>(words[11]);
    o.runtime.instruction_count=static_cast<decltype(o.runtime.instruction_count)>(words[12]);
    o.runtime.instruction.opcode=static_cast<decltype(o.runtime.instruction.opcode)>(words[13]);
    o.runtime.instruction.argument=static_cast<decltype(o.runtime.instruction.argument)>(words[14]);
    o.still.id=static_cast<decltype(o.still.id)>(words[15]);
    o.still_connectivity_error=static_cast<decltype(o.still_connectivity_error)>(words[16]);
    o.heater.target_celsius=values[0];
    o.heater.output_ticks=values[1];
    o.heater_sensors.temperatures[0].celsius=values[2];
    o.heater_sensors.temperatures[1].celsius=values[3];
    o.heater_sensors.temperatures[2].celsius=values[4];
    o.heater_sensors.temperatures[3].celsius=values[5];
    o.auxiliary.acceleration[0]=values[6];
    o.auxiliary.acceleration[1]=values[7];
    o.auxiliary.acceleration[2]=values[8];
    std::strncpy(o.session.name.data(),name,o.session.name.size()-1);o.runtime.step_name=step;o.runtime.substep_name=substep;std::strncpy(o.still_samples.current.error.data(),error,64);HeadDynamicPainter dynamic(painter,o);DisplayTextContext context_text;display_text_font(context_text,*display_font(static_cast<DisplayFontId>(args[8])),args[9]);dynamic.text_context=&context_text;const DisplayRect rect{static_cast<std::int32_t>(args[4]),static_cast<std::int32_t>(args[5]),static_cast<std::int32_t>(args[6]),static_cast<std::int32_t>(args[7])};HeadWidgetResult result=HeadWidgetResult::Ok;
    if(args[0]==0)result=dynamic.runtime_state();else if(args[0]==1)result=dynamic.runtime_info(rect);else if(args[0]==2)result=dynamic.pause(rect,static_cast<PauseReason>(args[3]));else if(args[0]==3)result=dynamic.level();else return head_instruction_text(text,32,o.runtime.instruction)?0:99;
    std::memcpy(pixels,display.pixels.data(),4096);out[0]=painter.dirty;out[1]=context.line;out[2]=context.count;return static_cast<unsigned>(result);
}
