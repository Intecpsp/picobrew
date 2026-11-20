#include "picoferm/hardware.hpp"
#include <cmath>
#include <cstring>

namespace picoferm { namespace oem {
void Adc::write(unsigned reg,unsigned value) {
    ports_.wire_begin_transmission(address);ports_.wire_write(static_cast<std::uint8_t>(reg));
    ports_.wire_write(static_cast<std::uint8_t>(value));ports_.wire_end_transmission();
}
std::uint8_t Adc::read(unsigned reg) {
    ports_.wire_begin_transmission(address);ports_.wire_write(static_cast<std::uint8_t>(reg));ports_.wire_end_transmission();
    ports_.wire_request(address,1);return static_cast<std::uint8_t>(ports_.wire_read());
}
void Adc::set(unsigned reg,unsigned bit) {write(reg,read(reg)|(1u<<(bit&31)));}
void Adc::clear(unsigned reg,unsigned bit) {write(reg,read(reg)&~(1u<<(bit&31)));}
void Adc::wait_set(unsigned reg,unsigned bit) {const auto mask=static_cast<std::uint8_t>(1u<<(bit&31));while((read(reg)&mask)==0){}}
void Adc::wait_clear(unsigned reg,unsigned bit) {const auto mask=static_cast<std::uint8_t>(1u<<(bit&31));while((read(reg)&mask)!=0){}}
void Adc::calibrate() {set(2,2);wait_clear(2,2);}
void Adc::configure_rate() {clear(2,4);set(2,5);clear(2,6);}
void Adc::configure_gain() {clear(1,2);clear(1,1);clear(1,0);}
void Adc::configure_reference() {set(0,7);set(1,5);clear(1,4);set(1,3);reference_voltage=3.0f;}
void Adc::initialize() {
    set(0,0);clear(0,0);set(0,1);wait_set(0,3);set(0,2);
    set(0x15,4);set(0x15,5);set(0x1b,0);clear(0x1b,4);calibrate();set(0x11,5);set(0,4);
}
bool Adc::begin(unsigned sda,unsigned scl,unsigned device) {
    address=static_cast<std::uint8_t>(device);ports_.wire_begin(sda&255,scl&255);initialize();return true;
}
std::uint32_t Adc::read24(unsigned reg) {
    ports_.wire_begin_transmission(address);ports_.wire_write(static_cast<std::uint8_t>(reg));ports_.wire_end_transmission();
    ports_.wire_request(address,3);const auto first=ports_.wire_read();const auto second=ports_.wire_read();const auto third=ports_.wire_read();
    return ((first<<8)|second)<<8|third;
}
std::int32_t Adc::sample() {
    wait_set(0,5);auto value=read24(0x12);set(0,4);
    if(value&0x800000u)value|=0xff000000u;
    std::int32_t result;std::memcpy(&result,&value,4);return result;
}
void Acquisition::start() {
    if(idle()){raw_temperature=0;raw_pressure=0;state=1;deadline=0;}
    else diagnostic_warning(ports_);
}
float Acquisition::temperature_fahrenheit(std::int32_t raw) {
    const double voltage=static_cast<double>(raw)*3.0*5.960464477539063e-8;
    const float resistance=static_cast<float>((voltage+1.5)*10000.0/(1.5-voltage));
    const float relative=resistance/10000.0f;
    const float denominator=static_cast<float>(std::log(static_cast<double>(relative))/3428.0+0.0033540164346805303);
    const float kelvin=1.0f/denominator;
    const float celsius=static_cast<float>(static_cast<double>(kelvin)-273.15);
    return static_cast<float>(static_cast<double>(celsius)*1.8+32.0);
}
void Acquisition::advance(std::uint32_t now) {
    static const std::uint32_t delays[13]={0,400,100,100,100,100,100,400,100,100,100,100,100};
    // Outside the scheduler's returning domain, the OEM reads beyond its delay
    // table. Reject that invalid state explicitly instead of an out-of-bounds read.
    if(state>12){diagnostic_assertion(ports_);return;}
    if(state==1)adc_.pressure_channel();
    else if(state==7)adc_.temperature_channel();
    else if(state==12)raw_temperature=adc_.sample();
    else if(state==6){raw_pressure=adc_.sample();if(raw_pressure<1)diagnostic_assertion(ports_);}
    else adc_.sample();
    deadline=now+delays[state];
    if(state==12)events_.measurement(temperature_fahrenheit(raw_temperature),static_cast<std::uint32_t>(raw_pressure));
    ++state;
}
void Acquisition::poll(HardwarePorts& hardware) {
    if(idle()){if(continuous&&complete())start();return;}
    const auto now=hardware.millis();if(deadline<now)advance(now);
}
float battery_voltage(HardwarePorts& ports) {return static_cast<float>(static_cast<double>(ports.analog_read(17))*0.00314);}

const AnimationDefinition animations[10]={
    {0,-1,0,{false,false,false}},{0,-1,0,{true,true,true}},{0,-1,0,{true,true,false}},
    {1,-1,250,{true,true,true}},{2,-1,250,{false,false,false}},
    {1,5,1000,{false,true,false}},{1,9,1000,{true,false,false}},
    {1,9,500,{false,true,true}},{1,9,500,{true,false,true}},{1,9,500,{false,false,true}}
};
const char* animation_name(Animation value) noexcept {
    static const char* const names[]={"Off","AccessPointMode","Updating","Busy","TalkingToPicobrew",
        "FermentationInProgress","FermentationDone","FermentationError","AccountNotAssociated","GenericError"};
    const auto i=static_cast<unsigned>(value);return i<10?names[i]:"Animation::INVALID";
}
void Leds::initialize(){for(auto pin:pins)hardware_.pin_mode(pin,1);for(auto pin:pins)ports_.digital_write(pin,true);}
void Leds::set_active(std::uint32_t value){for(unsigned i=0;i<3;++i)if(enabled[i])ports_.digital_write(pins[i],(value&255)!=0);}
void Leds::set(Animation value) {
    const auto now=hardware_.millis();if(value==animation)return;
    // The OEM indexes the table directly. Keep invalid animation requests out
    // of the owned table and distinguish this guard from OEM returning cases.
    if(static_cast<unsigned>(value)>=10){diagnostic_assertion(ports_);return;}
    for(auto pin:pins)ports_.digital_write(pin,true);
    animation=value;ports_.text("LED","ANIMATION",animation_name(value));events_.animation_changed(value);
    const auto& definition=animations[static_cast<unsigned>(value)];mode=definition.mode;steps=definition.steps;period=definition.period;
    for(unsigned i=0;i<3;++i)enabled[i]=definition.enabled[i];
    if(mode==0){for(auto pin:pins)ports_.digital_write(pin,true);set_active(0);}
    else {
        deadline=now+static_cast<std::uint32_t>(period);
        if(mode==1){toggle=1;set_active(0);}
        else if(mode==2){rotating_index=0;ports_.digital_write(pins[2],false);}
    }
}
void Leds::set_text(const char* value) {
    if(!value)return;
    // The console omits the Updating animation: text "2" selects Busy (3).
    static const Animation values[]={Animation::Off,Animation::AccessPointMode,Animation::Busy,Animation::TalkingToPicobrew,
        Animation::FermentationInProgress,Animation::FermentationDone,Animation::FermentationError,Animation::AccountNotAssociated,Animation::GenericError};
    if(value[0]>='0'&&value[0]<='8'&&value[1]=='\0')set(values[value[0]-'0']);
}
void Leds::poll() {
    const auto now=hardware_.millis();if(deadline>now)return;
    if(steps==0){set(Animation::Off);return;}
    if(mode==1){set_active(toggle);toggle=toggle==1?0:1;}
    else if(mode==2){
        if(rotating_index>2){diagnostic_assertion(ports_);return;}
        ports_.digital_write(pins[2-rotating_index],true);rotating_index=(rotating_index+1)%3;
        ports_.digital_write(pins[2-rotating_index],false);
    }
    deadline=now+static_cast<std::uint32_t>(period);if(steps>0)--steps;
}
void Button::initialize(std::uint8_t persisted_button) {
    hardware_.pin_mode(13,0);hardware_.pin_mode(15,1);woke_board=read_button(ports_)!=persisted_button;
    ports_.text("BTN","WOKE_BOARD",woke_board?"True":"False");
}
void Button::poll() {
    const auto now=hardware_.millis();const auto level=ports_.digital_read(13);
    if(level==0){
        if(!pressed){pressed=true;long_reported=false;deadline=now+2000;ports_.text("BTN","STATE","PRESSED");}
        else if(deadline<now&&!long_reported){ports_.text("BTN","EVENT","LONGPRESS");long_reported=true;events_.long_press();}
    }else if(pressed){pressed=false;ports_.text("BTN","STATE","RELEASED");}
}
}} // namespace picoferm::oem
