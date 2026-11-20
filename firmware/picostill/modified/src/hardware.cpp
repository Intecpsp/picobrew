// Modified from the recovered PicoStill 0.0.35 source.
// Foreground deadline scheduling/comparisons are repaired for uint32 millis
// rollover; the original reconstruction remains unchanged in reverse-engineered/.
#include "picostill/hardware.hpp"
#include <algorithm>
#include <cstring>

namespace picostill { namespace oem {
void OutputPin::set(bool value) {
    if (value==enabled) return;
    ports_.digital_write(pin,value); enabled=value; // 0x400d2ee4.
}
void OutputPin::set_pump(bool value) {
    if (value==enabled) return;
    ports_.digital_write(pin,value); enabled=value;
    ports_.variable("PUMP","IsRunning",value?"True":"False"); // 0x400d2f68.
}
void Fan::setup() {
    ports_.pin_mode(tach_pin,1);
    ports_.pwm_setup(channel,20.0,8); // Original frequency literal is 20 Hz.
    ports_.pwm_attach(output_pin,channel); // 0x400d2e3c.
}
void Fan::set(bool value, std::uint8_t duty) {
    if (value==enabled && duty==speed) return;
    enabled=value; speed=duty;
    ports_.pwm_write(channel,value?duty:0);
    metrics.rpm_average=0; metrics.samples=0;
    tach_high=ports_.digital_read(tach_pin);
    next_sample=static_cast<std::uint32_t>(ports_.millis()+2000u); // OEM high word cleared.
}
void Fan::sample() {
    const auto now=ports_.millis();
    const bool high=ports_.digital_read(tach_pin);
    if (high!=tach_high) { tach_high=high; if(high) ++pulses; }
    if (static_cast<std::int32_t>(now-static_cast<std::uint32_t>(next_sample))<0) return;
    const auto elapsed=static_cast<std::uint32_t>(now-static_cast<std::uint32_t>(next_sample))+50u;
    const float raw=static_cast<float>((static_cast<double>(pulses)/static_cast<double>(elapsed))*60000.0);
    if (metrics.samples>599) metrics.samples=5;
    ++metrics.samples;
    metrics.rpm_average=static_cast<float>(exponential_moving_average(metrics.rpm_average,
        static_cast<std::int32_t>(metrics.samples),static_cast<double>(raw*0.5f),diagnostics_));
    next_sample=std::uint64_t{now}+50u;
    pulses=0; // 0x400d4740.
}
void FanBank::set_power(bool value) {
    power.set(value); first.set(value,255); second.set(value,255);
    ports_.variable("FANS","HavePower",power.enabled?"True":"False"); // 0x400d2f00.
}
void FanBank::set_first(bool value) {
    power.set(value || second_enabled()); first.set(value,255); // 0x400d2f34.
}
void Leds::set_all(bool high) {
    for(unsigned i=0;i<3;++i) { ports_.digital_write(pins[i],high); values[i]=high; }
}
void Leds::toggle(unsigned color) {
    // Internal callers only use the three literal colors in the OEM parser.
    const bool next=!values[color]; ports_.digital_write(pins[color],next); values[color]=next;
}
void Leds::setup() {
    for(auto pin:pins) ports_.pin_mode(pin,2);
    set_all(false);
}
void Leds::clear_program() noexcept { program.fill(0); length=0; remaining=63; }
void Leds::start(const char* text, std::int32_t count) {
    set_all(false); clear_program();
    if(text && *text) {
        const auto size=std::strlen(text);
        diagnostics_.check(size<remaining,"datalen < _left Buffer not big enough");
        length=static_cast<std::uint32_t>(std::min(size,std::size_t{remaining}));
        std::memcpy(program.data(),text,length); remaining-=length;
    }
    repeat=count;
    diagnostics_.require(length>1 || (length==1 && count!=0),"Leds.h",0x122,
        "(_program.length() > 1) || (_program.length() == 1 && _repeatCount != 0)");
    completed=0; position=0; deadline=ports_.millis();
    ports_.variable("LED","PGM",program.data());
}
bool Leds::start(Animation animation) {
    const auto* selected=animation_program(animation);
    if(!selected) return false;
    ports_.variable("LED","ANIMATION",to_string(animation));
    start(selected->text,selected->repeat); return true;
}
void Leds::poll() {
    const auto now=ports_.millis();
    if(!length || static_cast<std::int32_t>(now-deadline)<0) return;
    diagnostics_.require(position<length,"Leds.h",0xe9,"_iptr < _program.length()");
    for(;;) {
        diagnostics_.check(position<length,"Buffer::charAt index < _idx ");
        const char command=program[position];
        bool waiting=false;
        if(command=='.') { deadline=now+250u; waiting=true; }
        else if(command=='-') { deadline=now+1000u; waiting=true; }
        else if(command=='g') toggle(0);
        else if(command=='y') toggle(1);
        else if(command=='r') toggle(2);
        if(++position>=length) {
            position=0; ++completed;
            if(repeat>0 && completed>=static_cast<std::uint32_t>(repeat)) { clear_program(); return; }
        }
        if(waiting) return;
    }
}
void PumpTest::set_state(PumpTestState next) {
    const auto number=static_cast<std::uint32_t>(next);
    diagnostics_.require(number<6,"HardwareTests.h",0x4e,"isEnumValid(newState)");
    switch(next) {
    case PumpTestState::CheckingVacuumSeal: deadline_runtime=60000; break;
    case PumpTestState::CheckingClog: deadline_runtime=vacuum_duration+60000; break;
    case PumpTestState::CheckingLeak: deadline_runtime=vacuum_duration+300000; break;
    default: if(number==6) diagnostics_.message("Invalid newState = PumpTestState::_EnumCount"); break;
    }
    hardware_.variable("HW_TEST","PUMP_STATE",to_string(next)); state=next; // 0x400d4a68.
}
void PumpTest::tick(std::uint64_t now, std::uint64_t runtime) {
    if(state==PumpTestState::Done) return;
    snapshots_.capture_into(current);
    const float pressure=current.values[4];
    switch(state) {
    case PumpTestState::Starting:
        initial_pressure=pressure; started_at=now; set_state(PumpTestState::CheckingVacuumSeal); break;
    case PumpTestState::CheckingVacuumSeal:
        if(runtime>deadline_runtime) set_state(PumpTestState::Evaluating);
        else if(pressure<=initial_pressure-1.5f) {
            vacuum_duration=now-started_at; set_state(PumpTestState::CheckingClog);
        }
        break;
    case PumpTestState::CheckingClog:
        if(runtime>deadline_runtime) { clog_pressure=pressure; set_state(PumpTestState::CheckingLeak); }
        else if(pressure<=4.0f) { clog_pressure=pressure; set_state(PumpTestState::Evaluating); }
        break;
    case PumpTestState::CheckingLeak:
        if(runtime<=deadline_runtime && pressure>4.0f) break;
        leak_pressure=pressure; set_state(PumpTestState::Evaluating); break;
    case PumpTestState::Evaluating:
        if(!vacuum_duration) error=StillError::NoVacuumSeal;
        else if(clog_pressure<=4.0f) error=StillError::PumpClogged;
        else if(leak_pressure>4.0f) error=StillError::PressureLeak;
        result=current; set_state(PumpTestState::Done); break;
    default:
        if(static_cast<std::uint32_t>(state)==6) diagnostics_.message("Invalid _state = PumpTestState::_EnumCount");
        break;
    }
}
}} // namespace picostill::oem
