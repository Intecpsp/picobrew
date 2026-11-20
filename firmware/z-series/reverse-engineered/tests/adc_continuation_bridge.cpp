#include "zseries/sensor_acquisition.hpp"
#include "zseries/core_flow.hpp"
#include "zseries/diagnostics.hpp"
#include <algorithm>
#include <vector>
using namespace zseries::oem;
namespace {
struct Fixture {
    TemperatureAcquisition acquisition;
    HeaterSensors sensors;
    DiagnosticState diagnostic;
    ConsoleServices console;
    I2cTransport bus{};
    AcquisitionClock clock{};
    const int* bytes=nullptr;
    std::size_t position=0,returned=0;
    std::uint32_t now=0;
    AcquisitionResult status=AcquisitionResult::Ok;
    std::vector<int> events;
    void event(int op,int a=0,int b=0,int c=0){events.insert(events.end(),{op,a,b,c});}
};
std::uint32_t hash(const char* p){std::uint32_t value=2166136261u;while(*p)value=(value^static_cast<unsigned char>(*p++))*16777619u;return value;}
std::uint32_t clock(void* p){auto& f=*static_cast<Fixture*>(p);f.event(9,static_cast<int>(f.now));return f.now;}
bool completed(AcquisitionResult r){return r==AcquisitionResult::Ok||r==AcquisitionResult::ShortReply||r==AcquisitionResult::ReadFailure||r==AcquisitionResult::InvalidClock;}
}
extern "C" int adc_continuation_action(unsigned kind,std::uint32_t* words,double* values,const int* bytes,
                                      int* events,std::size_t* event_count){
    Fixture f;f.now=words[3];f.returned=words[2];f.bytes=bytes;f.diagnostic.assertion_count=words[5];
    f.console.context=&f;
    f.console.assertion=[](void* p,const char* file,std::uint32_t line,const char* expression){
        auto& f=*static_cast<Fixture*>(p);DiagnosticServices d;d.context=p;d.millis=clock;
        d.assertion_log=[](void* p,double,const char* file,std::uint32_t line,const char* expression){
            static_cast<Fixture*>(p)->event(6,line,static_cast<int>(hash(file)),static_cast<int>(hash(expression)));};
        d.delay_ms=[](void* p,std::uint32_t ms){auto& f=*static_cast<Fixture*>(p);f.now+=ms;f.event(7,ms,f.diagnostic.assertion_count,static_cast<int>(f.now));};
        return record_assertion(f.diagnostic,d,file,line,expression)==DiagnosticResult::Ok;
    };
    f.console.log=[](void* p,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message){
        clock(p);auto& f=*static_cast<Fixture*>(p);f.event(8,line,static_cast<int>(hash(message)),static_cast<int>(level));
        f.event(10,static_cast<int>(hash(tag)),static_cast<int>(hash(file)));return true;
    };
    f.bus.context=&f;
    f.bus.request_from=[](void* p,std::uint8_t address,std::size_t count,bool stop){auto& f=*static_cast<Fixture*>(p);f.event(4,address,count,stop);return f.returned;};
    f.bus.read=[](void* p){auto& f=*static_cast<Fixture*>(p);const auto result=f.bytes[f.position++];f.event(5,result);return result;};
    f.clock={&f,clock};
    auto& a=f.acquisition;a.adc.address=words[0];a.adc.polling_enabled=words[1];
    for(unsigned i=0;i<4;++i){
        a.adc.raw[i]=static_cast<std::int32_t>(words[8+i]);a.adc.sensor_types[i]=static_cast<TempSensorType>(words[12+i]);
        f.sensors.temperatures[i].updated_ms=words[16+i];f.sensors.temperatures[i].celsius=values[i];
        auto& s=a.smoothers[i];const auto v=4+i*12,w=20+i*4;
        std::copy(values+v,values+v+10,s.samples.begin());s.sum=values[v+10];s.value=values[v+11];
        s.next=words[w];s.count=words[w+1];s.last_ms=words[w+2];s.value_ms=words[w+3];
    }
    int flow=0;
    if(kind==0)f.status=max11613_poll(a.adc,f.bus,&f.console);
    else{
        EepromData settings;CoreFlow core(settings);core.state.id=CoreStateId::StandBy;
        core.observed.temperature_polling_enabled=a.adc.polling_enabled;core.services.context=&f;core.services.millis=clock;
        core.services.perform=[](void*,const CoreAction&,std::uint32_t&){return false;};
        core.services.tick=[](void* p,CoreTickStage stage,std::uint32_t now){
            auto& f=*static_cast<Fixture*>(p);f.event(100,static_cast<int>(stage),static_cast<int>(now));
            if(stage!=CoreTickStage::Temperatures)return true;
            f.status=acquire_temperatures(f.acquisition,f.sensors,now,f.bus,f.clock,&f.console);return completed(f.status);
        };
        flow=static_cast<int>(core.tick(words[4]));
    }
    words[3]=f.now;words[5]=f.diagnostic.assertion_count;words[6]=static_cast<std::uint32_t>(f.status);words[7]=flow;
    for(unsigned i=0;i<4;++i){
        words[8+i]=static_cast<std::uint32_t>(a.adc.raw[i]);words[16+i]=f.sensors.temperatures[i].updated_ms;values[i]=f.sensors.temperatures[i].celsius;
        const auto& s=a.smoothers[i];const auto v=4+i*12,w=20+i*4;
        std::copy(s.samples.begin(),s.samples.end(),values+v);values[v+10]=s.sum;values[v+11]=s.value;
        words[w]=s.next;words[w+1]=s.count;words[w+2]=s.last_ms;words[w+3]=s.value_ms;
    }
    *event_count=f.events.size();std::copy(f.events.begin(),f.events.end(),events);return flow;
}
