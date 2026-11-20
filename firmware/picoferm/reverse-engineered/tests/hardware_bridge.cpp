#include "core_bridge.cpp"
#include "picoferm/hardware.hpp"
namespace {
struct FakeHardware final:HardwarePorts {
    std::uint32_t now=0,tick=0;std::int32_t analog=0;std::vector<std::uint32_t> reads;std::size_t index=0;
    std::uint32_t millis() override {const auto value=now;ports.events.push_back("millis:"+std::to_string(value));now+=tick;return value;}
    void pin_mode(unsigned pin,unsigned mode) override {ports.events.push_back("pinmode:"+std::to_string(pin)+":"+std::to_string(mode));}
    std::int32_t analog_read(unsigned pin) override {ports.events.push_back("analog:"+std::to_string(pin));return analog;}
    void wire_begin(unsigned sda,unsigned scl) override {ports.events.push_back("wire_begin:"+std::to_string(sda)+":"+std::to_string(scl));}
    void wire_begin_transmission(unsigned address) override {ports.events.push_back("wire_begin_transmission:"+std::to_string(address));}
    void wire_write(std::uint8_t value) override {ports.events.push_back("wire_write:"+std::to_string(value));}
    void wire_end_transmission() override {ports.events.push_back("wire_end");}
    void wire_request(unsigned address,unsigned count) override {ports.events.push_back("wire_request:"+std::to_string(address)+":"+std::to_string(count));}
    std::uint32_t wire_read() override {const auto value=reads.at(index++);ports.events.push_back("wire_read:"+std::to_string(value));return value;}
};
struct Events final:AcquisitionEvents,LedEvents,ButtonEvents {
    void measurement(float temperature,std::uint32_t raw) override {std::uint32_t bits;std::memcpy(&bits,&temperature,4);char b[9];std::snprintf(b,sizeof(b),"%08x",bits);ports.events.push_back(std::string("measurement:")+b+":"+std::to_string(raw));}
    void animation_changed(Animation animation) override {ports.events.push_back("animation:"+std::to_string(static_cast<std::uint32_t>(animation)));}
    void long_press() override {ports.events.push_back("long_press");}
};
std::uint32_t bits(float value){std::uint32_t out;std::memcpy(&out,&value,4);return out;}
}
extern "C" void ferm_hardware_run(unsigned op,std::uint32_t* w,const std::uint32_t* reads,unsigned count,const char* text){
    ports.events.clear();std::memset(result,0,sizeof(result));
    FakeHardware hw;hw.now=w[4];std::memcpy(&hw.analog,w+5,4);hw.reads.assign(reads,reads+count);
    Adc adc(hw);adc.address=w[0];adc.reference_voltage=number(w[1]);Events events;Acquisition acq(adc,ports,events);
    acq.state=w[8];acq.deadline=w[9];std::memcpy(&acq.raw_temperature,w+10,4);std::memcpy(&acq.raw_pressure,w+11,4);
    Leds leds(ports,hw,events);leds.animation=static_cast<Animation>(w[12]);std::memcpy(&leds.mode,w+13,4);std::memcpy(&leds.steps,w+14,4);std::memcpy(&leds.period,w+15,4);
    for(unsigned i=0;i<3;++i)leds.enabled[i]=w[16+i];
    leds.deadline=w[19];leds.toggle=w[20];leds.rotating_index=w[21];
    switch(op){
    case 0:adc.write(w[2],w[3]);break;case 1:result[0]=adc.read(w[2]);break;
    case 2:adc.set(w[2],w[3]);break;case 3:adc.clear(w[2],w[3]);break;
    case 4:adc.wait_set(w[2],w[3]);break;case 5:adc.wait_clear(w[2],w[3]);break;
    case 6:adc.calibrate();break;case 7:adc.initialize();break;case 8:result[0]=adc.begin(w[2],w[3],w[6]);break;
    case 9:result[0]=adc.read24(w[2]);break;case 10:result[0]=adc.sample();break;
    case 11:adc.configure_rate();break;case 12:adc.configure_gain();break;case 13:adc.configure_reference();break;
    case 14:acq.start();break;case 15:acq.advance(w[4]);break;case 16:result[0]=bits(battery_voltage(hw));break;
    case 17:leds.set(static_cast<Animation>(w[2]));break;case 18:leds.set_active(w[2]);break;case 19:leds.set_text(text);break;
    }
    w[0]=adc.address;w[1]=bits(adc.reference_voltage);w[8]=acq.state;w[9]=acq.deadline;w[10]=acq.raw_temperature;w[11]=acq.raw_pressure;
    w[12]=static_cast<unsigned>(leds.animation);w[13]=leds.mode;w[14]=leds.steps;w[15]=leds.period;
    for(unsigned i=0;i<3;++i)w[16+i]=leds.enabled[i];
    w[19]=leds.deadline;w[20]=leds.toggle;w[21]=leds.rotating_index;
}
