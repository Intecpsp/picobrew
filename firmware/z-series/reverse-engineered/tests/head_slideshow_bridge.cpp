#include "zseries/head_slideshow.hpp"
#include <cstring>
#include <cstdio>
#include <string>
using namespace zseries::oem;
namespace {
struct Context{std::uint32_t count=7,line=0,clock=777;std::string events;};
bool assertion(void* p,std::uint32_t line){auto& c=*static_cast<Context*>(p);c.line=line;++c.count;c.clock+=200;return true;}
bool assertion_full(void* p,const char* file,std::uint32_t line,const char* expression){auto& c=*static_cast<Context*>(p);c.events+="assertion|"+std::string(file)+"|"+std::to_string(line)+"|"+expression+"\n";return assertion(p,line);}
bool log(void* p,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message){static_cast<Context*>(p)->events+="log|"+std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+message+"\n";return true;}
std::uint32_t millis(void*p){return static_cast<Context*>(p)->clock;}
bool scope(void*p,bool begin,const char*tag,const char*file,std::uint32_t line,const char*name){static_cast<Context*>(p)->events+="scope|"+std::to_string(begin?1:0)+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+name+"\n";return true;}
bool post(void*,UiCommand){return true;}
}
extern "C" unsigned head_slideshow_test(const std::uint32_t* args,const std::uint32_t* words,const double* values,const char* name,const char* step,const char* substep,std::uint8_t* pixels,std::uint32_t* out,char* events,char* titles){
    DisplayState display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<std::uint8_t>((i*73+17)^(i>>3));HeadWidgetPainter painter(display);HeadDynamicObserved o;Context context;context.clock=args[10];painter.services.context=&context;painter.services.assertion=assertion;painter.language=static_cast<LanguageType>(args[2]);
    o.heater.started=static_cast<decltype(o.heater.started)>(words[0]);
    o.runtime.elapsed_seconds=static_cast<decltype(o.runtime.elapsed_seconds)>(words[1]);
    o.runtime.instruction_left_seconds=static_cast<decltype(o.runtime.instruction_left_seconds)>(words[2]);
    o.runtime.total_left_seconds=static_cast<decltype(o.runtime.total_left_seconds)>(words[3]);
    o.runtime.pid_active=static_cast<decltype(o.runtime.pid_active)>(words[4]);
    o.runtime.flag_b5=static_cast<decltype(o.runtime.flag_b5)>(words[5]);
    o.runtime.still.reference_date=static_cast<decltype(o.runtime.still.reference_date)>(words[6]);
    o.runtime.still.boil_date=static_cast<decltype(o.runtime.still.boil_date)>(words[7]);
    o.runtime.t1_drop_watch.enabled=static_cast<decltype(o.runtime.t1_drop_watch.enabled)>(words[8]);
    o.runtime.t1_drop_watch.triggered=static_cast<decltype(o.runtime.t1_drop_watch.triggered)>(words[9]);
    o.network=static_cast<decltype(o.network)>(words[10]);
    o.wifi.id=static_cast<decltype(o.wifi.id)>(words[11]);
    o.core.offline=static_cast<decltype(o.core.offline)>(words[12]);
    o.core.service_mode=static_cast<decltype(o.core.service_mode)>(words[13]);
    o.core.program_id=static_cast<decltype(o.core.program_id)>(words[14]);
    o.still_samples.current.pump_running=static_cast<decltype(o.still_samples.current.pump_running)>(words[15]);
    o.still_samples.t1.count=static_cast<decltype(o.still_samples.t1.count)>(words[16]);
    o.still_samples.t2.count=static_cast<decltype(o.still_samples.t2.count)>(words[17]);
    o.still_samples.pressure.count=static_cast<decltype(o.still_samples.pressure.count)>(words[18]);
    o.heater.target_celsius=values[0];
    o.heater_sensors.temperatures[0].celsius=values[1];
    o.heater_sensors.temperatures[1].celsius=values[2];
    o.heater_sensors.temperatures[2].celsius=values[3];
    o.heater_sensors.temperatures[3].celsius=values[4];
    o.heater_sensors.wort_rate=values[5];
    o.heater_sensors.hex_rate=values[6];
    o.auxiliary.pressure_pa=values[7];
    o.heater_sensors.estimated_boil_celsius=values[8];
    o.still_samples.current.temperature[0]=values[9];
    o.still_samples.current.temperature[1]=values[10];
    o.still_samples.current.temperature[2]=values[11];
    o.still_samples.current.pressure=values[12];
    o.still_samples.current.speed[4]=values[13];
    o.still_samples.seconds10.speed[0]=values[14];
    o.still_samples.seconds10.speed[1]=values[15];
    o.still_samples.seconds10.speed[2]=values[16];
    o.runtime.still.reference_wort=values[17];
    o.runtime.still.heat_loss=values[18];
    o.runtime.still.boil_t3=values[19];
    o.runtime.still.boil_scalar_978=values[20];
    o.runtime.t1_drop_watch.maximum=values[21];
    o.still_samples.t1.squared_deviations=values[22];
    o.still_samples.t2.squared_deviations=values[23];
    o.still_samples.pressure.squared_deviations=values[24];
    o.reference_pressure_pa=values[25];
    std::strncpy(o.session.name.data(),name,o.session.name.size()-1);o.runtime.step_name=step;o.runtime.substep_name=substep;
    EncoderSlideshow encoder;HeadSlideshow view(painter,o,encoder);view.services.context=&context;view.services.millis=millis;view.services.post=post;view.services.scope=scope;view.services.diagnostics.context=&context;view.services.diagnostics.assertion=assertion_full;view.services.diagnostics.log=log;view.imperial=args[1];
    HeadWidgetResult result=HeadWidgetResult::Ok;
    if(args[0]==0)result=view.draw(args[3],args[4]);
    else if(args[0]==4)result=view.self_test();
    else {
        if(args[0]==3)result=view.initialize_all();else result=view.initialize(args[5],UiCommand::None,args[1],args[6],args[7]);
        if(result==HeadWidgetResult::Ok&&args[8])result=view.select(args[8]);
        if(args[9]&1)o.runtime.pid_active=!o.runtime.pid_active;
        if(args[9]&2)o.still_samples.current.pump_running=!o.still_samples.current.pump_running;
        if(args[9]&4)o.network=o.network?0:2;
        if(result==HeadWidgetResult::Ok&&args[11]){context.clock=args[11];result=view.tick(args[11]);}
        if(result==HeadWidgetResult::Ok&&args[12]){context.clock=args[12];result=view.tick(args[12]);}
        if(result==HeadWidgetResult::Ok&&args[0]==2)result=view.reset();
    }
    if(args[0]!=0&&args[0]!=4){out[4]=encoder.selected;out[5]=encoder.count;out[6]=encoder.enabled;out[7]=encoder.timer.active;out[8]=encoder.timer.duration;out[9]=encoder.timer.deadline;for(unsigned i=0;i<12;++i){const auto& e=view.entries[i];auto* v=out+10+i*7;v[0]=e.type;v[1]=e.counter;v[2]=static_cast<std::uint32_t>(e.increment);v[3]=e.enabled;v[4]=e.timer.active;v[5]=e.timer.duration;v[6]=e.timer.deadline;std::memcpy(titles+i*21,e.title.data(),21);}}
    if(context.events.size()>=8192)return 99;
    std::memcpy(events,context.events.c_str(),context.events.size()+1);
    std::memcpy(pixels,display.pixels.data(),4096);out[0]=painter.dirty;out[1]=context.line;out[2]=context.count;out[3]=context.clock;return static_cast<unsigned>(result);
}
extern "C" unsigned head_slideshow_missing_services(){
    DisplayState display;HeadWidgetPainter painter(display);HeadDynamicObserved observed;EncoderSlideshow state;HeadSlideshow view(painter,observed,state);unsigned result=0;
    if(view.initialize(7,UiCommand::None,false,false,false)==HeadWidgetResult::MissingService)result|=1;
    if(view.select(0)==HeadWidgetResult::MissingService)result|=2;
    if(view.self_test()==HeadWidgetResult::MissingService)result|=4;
    state.enabled=true;if(view.button()==HeadWidgetResult::MissingService)result|=8;
    if(view.draw(0)==HeadWidgetResult::MissingService)result|=16;
    return result;
}
