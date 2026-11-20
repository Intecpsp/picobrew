#include "picostill/controller.hpp"
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
using namespace picostill::oem;
namespace {
std::uint64_t read(const std::uint8_t* p,unsigned offset,unsigned size=4){std::uint64_t n=0;for(unsigned i=0;i<size;++i)n|=std::uint64_t{p[offset+i]}<<(8*i);return n;}
void write(std::uint8_t* p,unsigned offset,std::uint64_t n,unsigned size=4){for(unsigned i=0;i<size;++i)p[offset+i]=static_cast<std::uint8_t>(n>>(8*i));}
float number(std::uint32_t n){float f;std::memcpy(&f,&n,4);return f;}
std::uint32_t bits(float f){std::uint32_t n;std::memcpy(&n,&f,4);return n;}
std::string hex(const void* p,unsigned size){std::string s;const auto* b=static_cast<const unsigned char*>(p);for(unsigned i=0;i<size;++i){char byte[3];std::snprintf(byte,sizeof(byte),"%02x",b[i]);s+=byte;}return s;}
struct Ports final: DiagnosticPorts,HardwarePorts,I2cPorts,ControllerPorts {
    std::vector<std::string> events;std::uint32_t clock=0;bool high=false;
    std::vector<int> pressure,adc,rx;unsigned position=0;
    void assertion(const char* file,unsigned line,const char* text) override{events.push_back(std::string("assertion:")+file+":"+std::to_string(line)+":"+text);}
    void message(const char* text) override{events.push_back(std::string("message:")+text);}
    void delay_ms(std::uint32_t duration) override{events.push_back("delay:"+std::to_string(duration));clock+=duration;}
    std::uint32_t millis() override{events.push_back("clock:"+std::to_string(clock));return clock;}
    void pin_mode(std::uint8_t pin,unsigned mode) override{events.push_back("pin_mode:"+std::to_string(pin)+":"+std::to_string(mode));}
    bool digital_read(std::uint8_t pin) override{events.push_back("read:"+std::to_string(pin)+":"+std::to_string(high));return high;}
    void digital_write(std::uint8_t pin,bool value) override{events.push_back("write:"+std::to_string(pin)+":"+std::to_string(value));}
    void pwm_setup(std::uint8_t,double,unsigned) override{}
    void pwm_attach(std::uint8_t,std::uint8_t) override{}
    void pwm_write(std::uint8_t channel,std::uint8_t duty) override{events.push_back("pwm_write:"+std::to_string(channel)+":"+std::to_string(duty));}
    void variable(const char* scope,const char* key,const char* value) override{events.push_back(std::string("variable:")+scope+":"+key+":"+value);}
    void begin_transmission(std::uint8_t address) override{events.push_back("begin:"+std::to_string(address));}
    void write_byte(std::uint8_t value) override{events.push_back("write_byte:"+std::to_string(value));}
    void end_transmission() override{events.push_back("end");}
    unsigned request(std::uint8_t address,unsigned count) override{events.push_back("request:"+std::to_string(address)+":"+std::to_string(count));rx=address==0x18?pressure:adc;position=0;return rx.size();}
    int available() override{events.push_back("available:"+std::to_string(rx.size()-position));return rx.size()-position;}
    int read_byte() override{const int value=position<rx.size()?rx[position++]:-1;events.push_back("read_byte:"+std::to_string(value));return value;}
    void sensors_sampled() override{events.push_back("sensors_sampled");}
    void snapshot_report(const HardwareSnapshot& snapshot) override{events.push_back("snapshot_report:"+hex(&snapshot,sizeof(snapshot)));}
    void full_test_report(const FullTestResult& result) override{events.push_back("full_test_report:"+hex(&result,sizeof(result)));}
    void controller_changed() override{events.push_back("controller_changed");}
    void console_message(const char* message) override{events.push_back(std::string("console:")+message);}
} ports;
Diagnostics diagnostics(ports);
std::vector<std::uint32_t> flags;
void load_fan(Fan& f,const std::uint8_t* b,unsigned p){
    f.channel=read(b,p,1);f.output_pin=read(b,p+1,1);f.tach_pin=read(b,p+2,1);f.next_sample=read(b,p+8,8);f.tach_high=read(b,p+16,1);
    f.enabled=read(b,p+17,1);f.speed=read(b,p+18,1);f.metrics.samples=read(b,p+20);f.metrics.rpm_average=number(read(b,p+24));f.pulses=read(b,p+28);
}
void save_fan(const Fan& f,std::uint8_t* b,unsigned p){
    write(b,p,f.channel,1);write(b,p+1,f.output_pin,1);write(b,p+2,f.tach_pin,1);write(b,p+8,f.next_sample,8);write(b,p+16,f.tach_high,1);
    write(b,p+17,f.enabled,1);write(b,p+18,f.speed,1);write(b,p+20,f.metrics.samples);write(b,p+24,bits(f.metrics.rpm_average));write(b,p+28,f.pulses);
}
void load(Hardware& h,Controller& c,const std::uint8_t* b,unsigned history=0xb00){
    load_fan(h.fans.first,b,0x3e0);load_fan(h.fans.second,b,0x400);h.fans.power.pin=read(b,0x3d8,1);h.fans.power.enabled=read(b,0x3d9,1);
    h.pump.pin=read(b,0x420,1);h.pump.enabled=read(b,0x421,1);h.error=static_cast<StillError>(read(b,0x3d0));std::memcpy(&h.error_snapshot,b+0x370,96);
    const unsigned s=0x428;h.sample_interval=read(b,s);h.failed_pressure=number(read(b,s+4));h.maximum_temperature=number(read(b,s+8));h.hysteresis=number(read(b,s+12));h.minimum_pressure=number(read(b,s+16));
    auto& a=h.sensors.adc;auto& p=h.sensors.pressure;
    a.address=read(b,s+0x1c,1);a.three_channels=read(b,s+0x1d,1);for(unsigned i=0;i<4;++i){a.raw[i]=read(b,s+0x20+4*i);a.valid[i]=read(b,s+0x30+i,1);}
    p.expected_status=read(b,s+0x38,1);p.address=read(b,s+0x39,1);p.range=read(b,s+0x3a,1);std::memcpy(&p.value,b+s+0x40,8);p.valid=read(b,s+0x48,1);
    h.next_sample=read(b,s+0x50);h.sensors_enabled=read(b,s+0x54,1);h.record_pressure=read(b,s+0x55,1);
    auto& m=h.sensors.metrics;m.temperature_samples=read(b,s+0x6c);m.pressure_samples=read(b,s+0x70);
    for(unsigned i=0;i<5;++i){h.sensors.values[i]=number(read(b,s+0x58+4*i));m.success_rates[i]=number(read(b,s+0x74+4*i));m.averages[i]=number(read(b,s+0x88+4*i));}
    for(unsigned i=0;i<200;++i)h.recorded_pressure[i]=number(read(b,history+4*i));
    h.recorded_count=read(b,history+800);
    c.state=static_cast<HWState>(read(b,0x4c8));c.state_changed_at=read(b,0x4d0,8);c.program=static_cast<ProgramId>(read(b,0x560));c.program_started_at=read(b,0x564);
    c.error=static_cast<StillError>(read(b,0x568));c.overtemperature_deadline=read(b,0x56c);c.overtemperature_holdoff=read(b,0xaa8);c.cleaning_duration=read(b,0xaac);std::memcpy(&c.full_result,b+8,56);
    auto& t=c.pump_test;t.state=static_cast<PumpTestState>(read(b,0x570));t.started_at=read(b,0x578,8);t.vacuum_duration=read(b,0x580,8);t.deadline_runtime=read(b,0x588,8);
    std::memcpy(&t.current,b+0x590,96);t.initial_pressure=number(read(b,0x5f0));t.clog_pressure=number(read(b,0x5f4));t.leak_pressure=number(read(b,0x5f8));t.error=static_cast<StillError>(read(b,0x5fc));std::memcpy(&t.result,b+0x600,96);
    auto& connection=c.connection_test;connection.started_at=read(b,0x660,8);connection.started=read(b,0x668,1);connection.done=read(b,0x669,1);connection.error=static_cast<StillError>(read(b,0x66c));std::memcpy(&connection.result,b+0x670,96);
    auto& pin=c.pin_test;pin.started_at=read(b,0x6d0,8);pin.started=read(b,0x6d8,1);pin.done=read(b,0x6d9,1);pin.sensors_passed=read(b,0x6da,1);std::memcpy(&pin.result,b+0x6e0,96);pin.error=static_cast<StillError>(read(b,0x740));
}
void save(const Hardware& h,const Controller& c,std::uint8_t* b,unsigned history=0xb00){
    save_fan(h.fans.first,b,0x3e0);save_fan(h.fans.second,b,0x400);write(b,0x3d9,h.fans.power.enabled,1);write(b,0x421,h.pump.enabled,1);
    write(b,0x3d0,static_cast<unsigned>(h.error));std::memcpy(b+0x370,&h.error_snapshot,96);const unsigned s=0x428;
    const auto& a=h.sensors.adc;const auto& p=h.sensors.pressure;
    for(unsigned i=0;i<4;++i){write(b,s+0x20+4*i,a.raw[i]);write(b,s+0x30+i,a.valid[i],1);}std::memcpy(b+s+0x40,&p.value,8);write(b,s+0x48,p.valid,1);
    write(b,s+0x50,h.next_sample);write(b,s+0x54,h.sensors_enabled,1);write(b,s+0x55,h.record_pressure,1);const auto& m=h.sensors.metrics;
    write(b,s+0x6c,m.temperature_samples);write(b,s+0x70,m.pressure_samples);
    for(unsigned i=0;i<5;++i){write(b,s+0x58+i*4,bits(h.sensors.values[i]));write(b,s+0x74+i*4,bits(m.success_rates[i]));write(b,s+0x88+i*4,bits(m.averages[i]));}
    for(unsigned i=0;i<200;++i)write(b,history+4*i,bits(h.recorded_pressure[i]));
    write(b,history+800,h.recorded_count);
    write(b,0x4c8,static_cast<unsigned>(c.state));write(b,0x4d0,c.state_changed_at,8);write(b,0x560,static_cast<unsigned>(c.program));write(b,0x564,c.program_started_at);
    write(b,0x568,static_cast<unsigned>(c.error));write(b,0x56c,c.overtemperature_deadline);std::memcpy(b+8,&c.full_result,56);
    const auto& t=c.pump_test;write(b,0x570,static_cast<unsigned>(t.state));write(b,0x578,t.started_at,8);write(b,0x580,t.vacuum_duration,8);write(b,0x588,t.deadline_runtime,8);
    std::memcpy(b+0x590,&t.current,96);write(b,0x5f0,bits(t.initial_pressure));write(b,0x5f4,bits(t.clog_pressure));write(b,0x5f8,bits(t.leak_pressure));write(b,0x5fc,static_cast<unsigned>(t.error));std::memcpy(b+0x600,&t.result,96);
    const auto& connection=c.connection_test;write(b,0x660,connection.started_at,8);write(b,0x668,connection.started,1);write(b,0x669,connection.done,1);write(b,0x66c,static_cast<unsigned>(connection.error));std::memcpy(b+0x670,&connection.result,96);
    const auto& pin=c.pin_test;write(b,0x6d0,pin.started_at,8);write(b,0x6d8,pin.started,1);write(b,0x6d9,pin.done,1);write(b,0x6da,pin.sensors_passed,1);std::memcpy(b+0x6e0,&pin.result,96);write(b,0x740,static_cast<unsigned>(pin.error));
}
}
extern "C" {
void controller_run(unsigned op,std::uint8_t* bytes,std::uint64_t arg,std::uint32_t now,int high,
                    const int* pressure,unsigned pressure_size,const int* adc,unsigned adc_size){
    ports.events.clear();flags.clear();ports.clock=now;ports.high=high;ports.pressure.assign(pressure,pressure+pressure_size);ports.adc.assign(adc,adc+adc_size);diagnostics.assertion_count=0;
    Hardware hardware(ports,ports,diagnostics,ports);Controller controller(hardware,ports,diagnostics,ports);load(hardware,controller,bytes);
    switch(op){
    case 0:hardware.enable_sensors(arg!=0);break;case 1:hardware.enable_sensors_internal(arg!=0);break;case 2:hardware.enable_pressure_recording();break;
    case 3:hardware.capture_into(hardware.error_snapshot);break;case 4:hardware.check_error();break;case 5:hardware.poll();break;
    case 6:controller.pin_test.tick(arg,hardware);break;case 7:controller.connection_test.tick(arg,hardware);break;
    case 8:controller.set_state(static_cast<HWState>(arg));break;case 9:controller.start(static_cast<ProgramId>(arg));break;
    case 10:controller.stop();break;case 11:controller.poll();break;
    case 12:flags.push_back(hardware.enabled());flags.push_back(hardware.sensors_ready());flags.push_back(hardware.fans_ready());
        flags.push_back(hardware.sensors_in_range());flags.push_back(hardware.fans_issue());flags.push_back(hardware.safe_to_resume());break;
    }
    save(hardware,controller,bytes);
}
unsigned controller_event_count(){return ports.events.size();}
const char* controller_event(unsigned i){return ports.events.at(i).c_str();}
std::uint32_t controller_assertions(){return diagnostics.assertion_count;}
std::uint32_t controller_clock(){return ports.clock;}
unsigned controller_flag_count(){return flags.size();}
std::uint32_t controller_flag(unsigned i){return flags.at(i);}
}
