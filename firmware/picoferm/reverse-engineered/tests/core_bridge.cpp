#include "picoferm/core.hpp"
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
using namespace picoferm::oem;
namespace {
struct TestPorts final: Ports {
    std::vector<std::string> events; std::uint32_t gpio=1;
    void yield() override { events.push_back("yield"); }
    void text(const char* scope,const char* key,const char* value) override {events.push_back(std::string("text:")+scope+":"+key+":"+value);}
    void integer(const char* scope,const char* key,std::uint32_t value) override {events.push_back(std::string("integer:")+scope+":"+key+":"+std::to_string(value));}
    void number(const char* scope,const char* key,float value) override {std::uint32_t bits;std::memcpy(&bits,&value,4);char out[9];std::snprintf(out,sizeof(out),"%08x",bits);events.push_back(std::string("number:")+scope+":"+key+":"+out);}
    void boolean(const char* scope,const char* key,bool value) override {events.push_back(std::string("boolean:")+scope+":"+key+":"+(value?"1":"0"));}
    void digital_write(unsigned pin,bool value) override {events.push_back("write:"+std::to_string(pin)+":"+(value?"1":"0"));}
    std::uint32_t digital_read(unsigned pin) override {events.push_back("read:"+std::to_string(pin)+":"+std::to_string(gpio));return gpio;}
    void persist(const std::uint8_t* bytes,std::size_t size) override {std::string out="persist:";for(std::size_t i=0;i<size;++i){char b[3];std::snprintf(b,sizeof(b),"%02x",bytes[i]);out+=b;}events.push_back(out);}
} ports;
std::uint32_t result[4];std::string message;
float number(std::uint32_t bits) {float value;std::memcpy(&value,&bits,4);return value;}
}
extern "C" {
void ferm_run(unsigned op,std::uint8_t* bytes,const std::uint32_t* words,const char* text) {
    ports.events.clear();ports.gpio=words[0];std::memset(result,0,sizeof(result));message.clear();
    Measurements measurements;auto& data=measurements.data;if(bytes)std::memcpy(data.bytes.data(),bytes,236);
    switch(op){
    case 0:diagnostic_assertion(ports);break;
    case 1:diagnostic_warning(ports);break;
    case 2:{Session session;result[2]=parse_session(session,text,ports);result[0]=static_cast<std::uint32_t>(session.state);result[1]=session.error_flags;break;}
    case 3:data.reset(ports);break;
    case 4:data.migrate(ports);break;
    case 5:data.sanitize(ports);break;
    case 6:{auto status=data.calibration(ports);result[0]=status.error;result[1]=status.can_calibrate;message=status.message;break;}
    case 7:data.append({number(words[1]),number(words[2])},ports);break;
    case 8:{LogIterator iterator(data);iterator.processed=words[1];iterator.index=words[2];result[0]=iterator.done();iterator.advance();result[1]=iterator.processed;result[2]=iterator.index;break;}
    case 9:result[0]=read_button(ports);break;
    case 10:case 11:measurements.machine_state=words[3];measurements.session_state=static_cast<SessionState>(words[4]);
        if(op==10)measurements.persist(ports);else measurements.on_adc(number(words[1]),words[2],ports);break;
    }
    if(bytes)std::memcpy(bytes,data.bytes.data(),236);
}
std::uint32_t ferm_result(unsigned index){return result[index];}
const char* ferm_message(){return message.c_str();}
unsigned ferm_event_count(){return ports.events.size();}
const char* ferm_event(unsigned index){return ports.events[index].c_str();}
int ferm_measurement(unsigned index,std::uint32_t* result){PersistentData data;Measurement out;bool valid=data.measurement(index,out);if(valid){std::memcpy(result,&out.temperature,4);std::memcpy(result+1,&out.pressure,4);}return valid;}
}
