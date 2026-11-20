#include "zseries/core_startup.hpp"
#include <cstdint>
using namespace zseries::oem;
namespace {
struct Trace {
    std::uint32_t* output;
    std::uint32_t count;
    unsigned boiler;
    int fail_stage;
    void event(std::uint32_t kind, std::uint32_t a=0, std::uint32_t b=0, std::uint32_t c=0, std::uint32_t d=0) {
        output[count*5]=kind;output[count*5+1]=a;output[count*5+2]=b;output[count*5+3]=c;output[count*5+4]=d;++count;
    }
};
Trace& trace(void* p) { return *static_cast<Trace*>(p); }
bool component(void* p, CoreSetupStage stage) { trace(p).event(0,static_cast<unsigned>(stage));return static_cast<int>(stage)!=trace(p).fail_stage; }
bool boiler(void* p, BoilerType& out) { out=static_cast<BoilerType>(trace(p).boiler);trace(p).event(3,trace(p).boiler);return true; }
void configure(void* p,std::uint8_t pin) { trace(p).event(1,pin,2); }
void gpio(void* p,std::uint8_t pin,bool value) { trace(p).event(2,pin,value); }
void begin(void* p,std::uint8_t address) { trace(p).event(4,address); }
std::size_t write(void* p,std::uint8_t value) { trace(p).event(5,value);return 1; }
void delay(void* p,std::uint32_t value) { trace(p).event(6,value); }
std::uint8_t end(void* p,bool stop) { trace(p).event(7,stop);return 0; }
bool software(void* p,std::uint32_t period,std::uintptr_t id,CoreTimerCallback cb,std::uint32_t block,PlatformHandle& out) {
    trace(p).event(8,period,static_cast<std::uint32_t>(id),static_cast<unsigned>(cb),block);out=0x8002;return trace(p).fail_stage!=-2;
}
PlatformHandle timer(void* p,std::uint8_t id,std::uint16_t divider,bool up) { trace(p).event(9,id,divider,up);return trace(p).fail_stage==-3?0:0x8000+id; }
void attach(void* p,PlatformHandle handle,CoreTimerCallback cb,bool edge) { trace(p).event(10,static_cast<std::uint32_t>(handle),static_cast<unsigned>(cb),edge); }
void alarm(void* p,PlatformHandle handle,std::uint64_t ticks,bool reload) { trace(p).event(11,static_cast<std::uint32_t>(handle),static_cast<std::uint32_t>(ticks),static_cast<std::uint32_t>(ticks>>32),reload); }
void enable(void* p,PlatformHandle handle,bool value) { trace(p).event(12,static_cast<std::uint32_t>(handle),value); }
}

extern "C" int core_startup_trace(unsigned kind,int fail_stage,int missing,std::uint32_t* events,std::uint32_t* count) {
    Trace state={events,0,kind,fail_stage};
    CoreStartupServices services={&state,component,boiler,software,timer,attach,alarm,enable};
    I2cTransport bus={&state,begin,write,end,nullptr,nullptr,delay};
    HeaterIo io={&state,configure,gpio};
    if(missing)services.setup_component=nullptr;
    HeaterController heater;TemperatureAcquisition acquisition;CoreTimerHandles handles;
    const auto result=setup_core_controls(acquisition,heater,handles,bus,io,services);
    *count=state.count;return static_cast<int>(result);
}
