#include "zseries/auxiliary_sensors.hpp"
#include <algorithm>
#include <vector>
using namespace zseries::oem;
namespace {
struct Bus {
    const int* ends; std::size_t end_count, end_index;
    const int* replies; std::size_t reply_count;
    const int* bytes; std::size_t position;
    int reg, writes; std::uint32_t now;
    std::vector<int> events;
    void event(int op, int a=0, int b=0, int c=0) { events.insert(events.end(), {op,a,b,c}); }
};
void begin(void* p, std::uint8_t address) { auto& b=*static_cast<Bus*>(p);b.writes=0;b.event(0,address); }
std::size_t write(void* p, std::uint8_t value) { auto& b=*static_cast<Bus*>(p);if(b.writes++==0)b.reg=value;b.event(1,value);return 1; }
std::uint8_t end(void* p, bool stop) { auto& b=*static_cast<Bus*>(p);b.event(2,stop);const auto i=b.end_index++;return i<b.end_count?b.ends[i]:0; }
std::size_t request(void* p, std::uint8_t address, std::size_t count, bool stop) {
    auto& b=*static_cast<Bus*>(p);b.event(4,address,count,stop);
    for(std::size_t i=0;i<b.reply_count;++i){const auto* reply=b.replies+i*26;if(reply[0]==b.reg){b.bytes=reply+2;b.position=0;return reply[1];}}
    b.bytes=nullptr;return 0;
}
int read(void* p) {auto& b=*static_cast<Bus*>(p);const int value=b.bytes[b.position++];b.event(5,value);return value;}
std::uint32_t clock(void* p) {return static_cast<Bus*>(p)->now;}
}
extern "C" int auxiliary_action(unsigned kind, double* values, std::int64_t* words,
 const int* ends, std::size_t end_count, const int* replies, std::size_t reply_count,
 int* output_events, std::size_t* event_size, int missing) {
    Bus b{ends,end_count,0,replies,reply_count,nullptr,0,0,0,static_cast<std::uint32_t>(words[9]),{}};
    I2cTransport io{&b,begin,write,end,request,missing?nullptr:read,nullptr};
    AcquisitionClock time{&b,clock};
    AuxiliarySensors sensors;for(unsigned i=0;i<3;++i)sensors.acceleration[i]=values[i];
    sensors.pressure_pa=values[3];sensors.pressure_ms=words[8];
    HeaterSensors heater;heater.estimated_boil_celsius=values[4];
    std::uint32_t result;
    if(kind<2){
        Lis3dhtr device;device.address=words[0];device.polling_enabled=words[1];
        for(unsigned i=0;i<3;++i)device.raw[i]=words[2+i];
        result=kind==0?lis3dhtr_setup(device,io):lis3dhtr_tick(device,sensors,io);
        words[1]=device.polling_enabled;for(unsigned i=0;i<3;++i)words[2+i]=device.raw[i];
    }else{
        Bmp280 device;device.address=words[0];device.polling_enabled=words[1];
        device.raw_pressure=words[5];device.raw_temperature=words[6];device.temperature_fine=words[7];
        auto& c=device.calibration;c.t1=words[10];c.t2=words[11];c.t3=words[12];c.p1=words[13];
        c.p2=words[14];c.p3=words[15];c.p4=words[16];c.p5=words[17];c.p6=words[18];c.p7=words[19];c.p8=words[20];c.p9=words[21];
        result=kind==2?bmp280_setup(device,io):kind==3?bmp280_read_calibration(device,io):bmp280_tick(device,sensors,heater,io,time);
        words[1]=device.polling_enabled;words[5]=device.raw_pressure;words[6]=device.raw_temperature;words[7]=device.temperature_fine;
        const std::int64_t output[]={c.t1,c.t2,c.t3,c.p1,c.p2,c.p3,c.p4,c.p5,c.p6,c.p7,c.p8,c.p9};std::copy(output,output+12,words+10);
    }
    for(unsigned i=0;i<3;++i)values[i]=sensors.acceleration[i];values[3]=sensors.pressure_pa;values[4]=heater.estimated_boil_celsius;
    words[8]=sensors.pressure_ms;*event_size=b.events.size();std::copy(b.events.begin(),b.events.end(),output_events);return result;
}
