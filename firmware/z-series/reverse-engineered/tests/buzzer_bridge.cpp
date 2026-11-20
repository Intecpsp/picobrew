#include "zseries/buzzer.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
struct Port {
    std::vector<std::uint32_t> events;
    std::uint32_t now, step;
    void event(std::uint32_t code, std::uint32_t a=0, std::uint32_t b=0,
               std::uint32_t c=0, std::uint32_t d=0, std::uint32_t f=0) {
        events.insert(events.end(), {code,a,b,c,d,f});
    }
};
std::uint32_t clock(void* context) {
    auto& p=*static_cast<Port*>(context);const auto now=p.now;p.now+=p.step;p.event(3,now);return now;
}
bool attach(void* c,std::uint8_t pin,std::uint8_t channel) {static_cast<Port*>(c)->event(0,pin,channel);return true;}
bool configure(void* c,std::uint8_t channel,double frequency,std::uint8_t resolution) {
    std::uint64_t bits;std::memcpy(&bits,&frequency,sizeof bits);
    static_cast<Port*>(c)->event(1,channel,bits,bits>>32,resolution);return true;
}
bool write(void* c,std::uint8_t channel,std::uint32_t duty) {static_cast<Port*>(c)->event(2,channel,duty);return true;}
bool begin(void* c,const char* name) {
    if(std::strcmp(name,"Buzzer"))return false;
    static_cast<Port*>(c)->event(4);return true;
}
bool command(void* c,Buzzer&,const BuzzerCommandDefinition& value) {
    static const char* names[]={"i","on","off","setf ","setd "};
    static const char* descriptions[]={"Displays Buzzer state","Turn buzzer on","Turn buzzer off","Sets buzzer frequency","Sets buzzer duty cycle"};
    const auto i=static_cast<unsigned>(value.command);
    if(i>=5||std::strcmp(value.name,names[i])||std::strcmp(value.description,descriptions[i]))return false;
    static_cast<Port*>(c)->event(5,i,value.exact_match);return true;
}
std::int32_t decimal(void* c,const char* text) {const auto result=std::atoi(text);static_cast<Port*>(c)->event(6,static_cast<std::uint32_t>(result));return result;}
bool assertion(void* c,std::uint32_t line) {static_cast<Port*>(c)->event(8,line);return true;}
bool diagnostic(void* c,BuzzerDiagnostic kind,std::uint32_t now,const BuzzerState& state,bool enabled) {
    static_cast<Port*>(c)->event(7,static_cast<unsigned>(kind),now,
        kind==BuzzerDiagnostic::Information?state.enabled:enabled,
        kind==BuzzerDiagnostic::Information?state.frequency_hz:0,
        kind==BuzzerDiagnostic::Information?state.duty:0);return true;
}
}
extern "C" unsigned buzzer_test(const std::uint32_t* w,const std::uint32_t* actions,unsigned count,
        const char* text,std::uint32_t* output,std::uint32_t* events,unsigned capacity) {
    Buzzer b;Port p;SystemAlarmState alarm;
    b.state.channel=w[0];b.state.resolution_bits=w[1];b.state.pin=w[2];b.state.frequency_hz=w[3];
    b.state.duty=w[4];b.state.enabled=w[5]!=0;b.state.previous_enabled=w[6]!=0;b.state.phase=w[7]!=0;
    alarm.enabled=w[8]!=0;alarm.changed_ms=w[9];p.now=w[10];p.step=w[11];
    b.services.context=&p;b.services.millis=clock;b.services.attach=attach;b.services.configure=configure;
    b.services.write=write;b.services.begin_console=begin;b.services.add_command=command;
    b.services.decimal_integer=decimal;b.services.assertion=assertion;b.services.diagnostic=diagnostic;
    BuzzerResult result=BuzzerResult::Ok;
    for(unsigned i=0;i<count;i++) {
        const auto op=actions[2*i],argument=actions[2*i+1];
        switch(op) {
        case 0:result=b.set_enabled(argument!=0);break;
        case 1:result=b.tick();break;
        case 2:result=b.set_alarm(alarm,argument!=0);break;
        case 3:result=b.setup();break;
        case 4:result=b.register_commands();break;
        case 5:case 6:case 7:case 8:case 9:result=b.command(static_cast<BuzzerCommand>(op-5),text);break;
        case 10:b.state=BuzzerState();break;
        default:return 91;
        }
        if(result!=BuzzerResult::Ok)break;
    }
    if(p.events.size()>capacity)return 92;
    const std::uint32_t state[]={b.state.channel,b.state.resolution_bits,b.state.pin,b.state.frequency_hz,
        b.state.duty,b.state.enabled,b.state.previous_enabled,b.state.phase,alarm.enabled,alarm.changed_ms,
        p.now,p.step,static_cast<std::uint32_t>(result),b.assertion_line,static_cast<std::uint32_t>(p.events.size())};
    std::memcpy(output,state,sizeof state);
    if(!p.events.empty())std::memcpy(events,p.events.data(),p.events.size()*sizeof(std::uint32_t));
    return 0;
}
