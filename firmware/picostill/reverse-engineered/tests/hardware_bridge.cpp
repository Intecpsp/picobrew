#include "picostill/hardware.hpp"
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
using namespace picostill::oem;
namespace {
float number(std::uint32_t raw) {float f;std::memcpy(&f,&raw,4);return f;}
std::uint32_t word(float f) {std::uint32_t raw;std::memcpy(&raw,&f,4);return raw;}
struct Ports : DiagnosticPorts, HardwarePorts, SnapshotPorts {
    std::vector<std::string> events;
    std::uint32_t clock=0,pressure=0;
    bool high=false;
    void assertion(const char* file,unsigned line,const char* text) override {
        events.push_back(std::string("assertion:")+file+":"+std::to_string(line)+":"+text);
    }
    void message(const char* text) override {events.push_back(std::string("message:")+text);}
    void delay_ms(std::uint32_t value) override {events.push_back("delay:"+std::to_string(value));}
    std::uint32_t millis() override {events.push_back("clock:"+std::to_string(clock));return clock;}
    void pin_mode(std::uint8_t pin,unsigned mode) override {events.push_back("mode:"+std::to_string(pin)+":"+std::to_string(mode));}
    bool digital_read(std::uint8_t pin) override {events.push_back("read:"+std::to_string(pin)+":"+std::to_string(high));return high;}
    void digital_write(std::uint8_t pin,bool value) override {events.push_back("write:"+std::to_string(pin)+":"+std::to_string(value));}
    void pwm_setup(std::uint8_t channel,double frequency,unsigned resolution) override {
        std::ostringstream out;out<<std::fixed<<std::setprecision(1)<<frequency;
        events.push_back("pwm_setup:"+std::to_string(channel)+":"+out.str()+":"+std::to_string(resolution));
    }
    void pwm_attach(std::uint8_t pin,std::uint8_t channel) override {events.push_back("pwm_attach:"+std::to_string(pin)+":"+std::to_string(channel));}
    void pwm_write(std::uint8_t channel,std::uint8_t duty) override {events.push_back("pwm_write:"+std::to_string(channel)+":"+std::to_string(duty));}
    void variable(const char* scope,const char* name,const char* value) override {events.push_back(std::string("variable:")+scope+":"+name+":"+value);}
    HardwareSnapshot capture() override {
        events.push_back("snapshot");std::array<std::uint32_t,24> words;
        for(unsigned i=0;i<24;++i)words[i]=i==9?pressure:0x3f000000u+i;
        HardwareSnapshot snapshot;
        snapshot.captured_at=std::uint64_t{words[0]}|(std::uint64_t{words[1]}<<32);
        snapshot.sensor_samples=words[2];snapshot.fan1_samples=words[3];snapshot.fan2_samples=words[4];
        for(unsigned i=0;i<5;++i){snapshot.values[i]=number(words[i+5]);snapshot.averages[i]=number(words[i+10]);snapshot.success_rates[i]=number(words[i+15]);}
        snapshot.fan1_speed=words[20];snapshot.fan1_rpm=number(words[21]);snapshot.fan2_speed=words[22];snapshot.fan2_rpm=number(words[23]);
        return snapshot;
    }
} ports;
Diagnostics diagnostics(ports);
std::vector<std::uint32_t> result;
void fan_words(const Fan& f) {
    const std::uint32_t words[]={f.channel,f.output_pin,f.tach_pin,static_cast<std::uint32_t>(f.next_sample),
        static_cast<std::uint32_t>(f.next_sample>>32),f.tach_high,f.enabled,f.speed,f.metrics.samples,word(f.metrics.rpm_average),f.pulses};
    result.insert(result.end(),std::begin(words),std::end(words));
}
void append64(std::uint64_t value) {result.push_back(static_cast<std::uint32_t>(value));result.push_back(static_cast<std::uint32_t>(value>>32));}
void snapshot_words(const HardwareSnapshot& s) {
    std::array<std::uint32_t,24> words;std::memcpy(words.data(),&s,sizeof(s));result.insert(result.end(),words.begin(),words.end());
}
std::uint64_t wide(const std::uint32_t* w) {return std::uint64_t{w[0]}|(std::uint64_t{w[1]}<<32);}
}
extern "C" {
void hardware_run(unsigned op,const std::uint32_t* words,const std::uint32_t* steps,unsigned count,const char* text) {
    ports.events.clear();ports.clock=0;ports.high=false;diagnostics.assertion_count=0;result.clear();
    if(op==0) {
        OutputPin pin(ports,19);pin.enabled=words[0]!=0;
        for(unsigned i=0;i<count;++i){const auto* s=steps+i*2;if(s[0]==0)pin.set(s[1]!=0);else pin.set_pump(s[1]!=0);}
        result.push_back(pin.enabled);
    }else if(op==1) {
        Fan fan(ports,diagnostics,words[0],words[1],words[2]);fan.next_sample=wide(words+3);fan.tach_high=words[5]!=0;
        fan.enabled=words[6]!=0;fan.speed=words[7];fan.metrics.samples=words[8];fan.metrics.rpm_average=number(words[9]);fan.pulses=words[10];
        for(unsigned i=0;i<count;++i){const auto* s=steps+i*5;ports.clock=s[1];ports.high=s[2]!=0;
            if(s[0]==0)fan.setup();else if(s[0]==1)fan.set(s[3]!=0,s[4]);else fan.sample();}
        fan_words(fan);
    }else if(op==2) {
        FanBank bank(ports,diagnostics);bank.power.enabled=words[0]!=0;bank.first.enabled=words[1]!=0;bank.second.enabled=words[2]!=0;
        for(unsigned i=0;i<count;++i){const auto* s=steps+i*4;ports.clock=s[1];ports.high=s[2]!=0;
            if(s[0]==0)bank.set_power(s[3]!=0);else bank.set_first(s[3]!=0);}
        result.push_back(bank.power.enabled);fan_words(bank.first);fan_words(bank.second);
    }else if(op==3) {
        Leds led(ports,diagnostics);
        for(unsigned i=0;i<count;++i){const auto* s=steps+i*3;ports.clock=s[1];
            switch(s[0]){case 0:led.setup();break;case 1:led.set_all(s[2]!=0);break;
            case 2:led.start(text,static_cast<std::int32_t>(s[2]));break;case 3:led.start(static_cast<Animation>(s[2]));break;default:led.poll();break;}}
        for(bool value:led.values)result.push_back(value);
        for(char c:led.program)result.push_back(static_cast<unsigned char>(c));
        const std::uint32_t state[]={led.length,led.remaining,static_cast<std::uint32_t>(led.repeat),led.completed,led.position,led.deadline};
        result.insert(result.end(),std::begin(state),std::end(state));
    }else if(op==4) {
        PumpTest test(ports,diagnostics,ports);test.state=static_cast<PumpTestState>(words[0]);test.started_at=wide(words+1);
        test.vacuum_duration=wide(words+3);test.deadline_runtime=wide(words+5);test.initial_pressure=number(words[7]);
        test.clog_pressure=number(words[8]);test.leak_pressure=number(words[9]);test.error=static_cast<StillError>(words[10]);
        for(unsigned i=0;i<count;++i){const auto* s=steps+i*6;if(s[0]==0)test.set_state(static_cast<PumpTestState>(s[3]));else{
            ports.pressure=s[5];test.tick(wide(s+1),wide(s+3));}}
        result.push_back(static_cast<std::uint32_t>(test.state));append64(test.started_at);append64(test.vacuum_duration);append64(test.deadline_runtime);
        snapshot_words(test.current);result.push_back(word(test.initial_pressure));result.push_back(word(test.clog_pressure));
        result.push_back(word(test.leak_pressure));result.push_back(static_cast<std::uint32_t>(test.error));snapshot_words(test.result);
    }
}
unsigned hardware_result_count(){return static_cast<unsigned>(result.size());}
std::uint32_t hardware_result(unsigned i){return result.at(i);}
unsigned hardware_event_count(){return static_cast<unsigned>(ports.events.size());}
const char* hardware_event(unsigned i){return ports.events.at(i).c_str();}
std::uint32_t hardware_assertions(){return diagnostics.assertion_count;}
}
