#include "picostill/core.hpp"
#include <cstring>
#include <string>
#include <vector>
using namespace picostill::oem;
namespace {
struct Ports : DiagnosticPorts, ButtonPorts {
    std::vector<std::string> events;
    std::uint32_t clock = 1234;
    bool high = true;
    Button* button = nullptr;
    void assertion(const char* file, unsigned line, const char* text) override {
        events.push_back(std::string("assertion:")+file+":"+std::to_string(line)+":"+text);
    }
    void message(const char* text) override { events.push_back(std::string("message:")+text); }
    void delay_ms(std::uint32_t value) override { events.push_back("delay:"+std::to_string(value)); }
    std::uint32_t millis() override { events.push_back("clock:"+std::to_string(clock)); return clock; }
    bool gpio_high() override { events.push_back("gpio:"+std::to_string(high)); return high; }
    void button_event(bool is_long) override {
        events.push_back("button:"+std::to_string(is_long)+":"+std::to_string(button->press_count));
    }
} ports;
Diagnostics diagnostic(ports);
float as_float(std::uint32_t value) { float out; std::memcpy(&out, &value, sizeof(out)); return out; }
}
extern "C" {
void native_reset(std::uint32_t seed) {
    ports.events.clear(); ports.clock=1234; ports.high=true; ports.button=nullptr; diagnostic.assertion_count=seed;
}
std::uint32_t native_assertion_count() { return diagnostic.assertion_count; }
unsigned native_event_count() { return static_cast<unsigned>(ports.events.size()); }
const char* native_event(unsigned i) { return ports.events.at(i).c_str(); }
double native_ema(double previous, std::int32_t count, double sample) {
    return exponential_moving_average(previous, count, sample, diagnostic);
}
double native_thermistor(const double* p, std::int32_t adc) {
    const ThermistorParameters parameters={p[0],p[1],p[2],p[3],static_cast<float>(p[4])};
    return thermistor_kelvin(parameters, adc, diagnostic);
}
void native_sensors(const std::uint32_t* counts, const std::uint32_t* bits, std::uint32_t* out) {
    SensorMetrics metrics; metrics.temperature_samples=counts[0]; metrics.pressure_samples=counts[1];
    for(unsigned i=0;i<5;++i) { metrics.success_rates[i]=as_float(bits[i]); metrics.averages[i]=as_float(bits[i+5]); }
    out[0]=metrics.sampling_threshold_met(); out[1]=metrics.read_success_rates_good(diagnostic);
    out[2]=metrics.values_in_range(diagnostic);
}
void native_fan(const std::uint32_t* words, std::uint32_t* out) {
    std::array<FanStatus,2> fans;
    for(unsigned i=0;i<2;++i) {
        const auto* w=words+i*4; fans[i].enabled=w[0]!=0; fans[i].speed=static_cast<std::uint8_t>(w[1]);
        fans[i].metrics.samples=w[2]; fans[i].metrics.rpm_average=as_float(w[3]);
    }
    out[0]=fans_have_issue(fans,diagnostic); out[1]=fans[0].metrics.running_fast_enough(diagnostic);
}
std::uint32_t native_pump(const std::uint32_t* bits) {
    return safe_to_resume_pump(as_float(bits[0]),as_float(bits[1]),as_float(bits[2]),as_float(bits[3]),as_float(bits[4]));
}
void native_errors(std::uint32_t error, std::uint32_t existing, std::uint32_t* out) {
    out[0]=static_cast<std::uint32_t>(error_animation(static_cast<StillError>(error),diagnostic));
    auto current=static_cast<StillError>(existing); latch_first_error(current,static_cast<StillError>(error),diagnostic);
    out[1]=static_cast<std::uint32_t>(current);
}
void native_button(const std::uint32_t* words, const std::uint32_t* steps, unsigned count, std::uint32_t* out) {
    Button button(ports,diagnostic); ports.button=&button; button.pressed=words[0]!=0; button.press_count=words[1];
    button.state=static_cast<ButtonState>(words[2]); button.changed_at=words[3];
    for(unsigned i=0;i<count;++i) {
        const auto* s=steps+i*4; ports.clock=s[1]; ports.high=s[2]!=0;
        if(s[0]==0)button.set_state(static_cast<ButtonState>(s[3]));
        else if(s[0]==1)button.on_edge(s[3]!=0);
        else button.poll();
    }
    out[0]=button.pressed; out[1]=button.press_count; out[2]=static_cast<std::uint32_t>(button.state); out[3]=button.changed_at;
    ports.button=nullptr;
}
const char* native_enum(unsigned which, unsigned value) {
    switch(which) {
    case 0:return to_string(static_cast<ProgramId>(value));
    case 1:return to_string(static_cast<StillError>(value));
    case 2:return to_string(static_cast<StillState>(value));
    case 3:return to_string(static_cast<PumpTestState>(value));
    case 4:return to_string(static_cast<HWState>(value));
    case 5:return to_string(static_cast<EventType>(value));
    case 6:return to_string(static_cast<WifiState>(value));
    case 7:return to_string(static_cast<Animation>(value));
    default:return nullptr;
    }
}
}
