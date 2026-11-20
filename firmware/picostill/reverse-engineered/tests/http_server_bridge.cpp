#include "controller_bridge.cpp"
#include "picostill/http_server.hpp"
#include <cmath>
namespace {
struct Runtime final:HttpRuntime {
    unsigned tick=1;
    std::uint32_t millis() override{return ports.millis();}
    void cooperate() override{ports.events.push_back("yield");ports.clock+=tick;}
    void variable(const char* scope,const char* key,const char* value) override{ports.variable(scope,key,value);}
    void integer(const char* scope,const char* key,std::uint32_t value) override{ports.events.push_back(std::string("integer:")+scope+":"+key+":"+std::to_string(value));}
    void message(const char* value) override{ports.message(value);}
} runtime;
struct Stream final:HttpStream {
    std::string input,output;unsigned position=0;
    bool connected() override{ports.events.push_back("connected:1");return true;}
    int available() override{int result=position<input.size()?1:0;ports.events.push_back("available:"+std::to_string(result));return result;}
    int read() override{int result=static_cast<unsigned char>(input[position++]);ports.events.push_back("read:"+std::to_string(result));return result;}
    std::size_t write(const char* bytes,std::size_t size) override{output.append(bytes,size);return size;}
    void flush() override{ports.events.push_back("flush");}
    void stop() override{ports.events.push_back("stop");}
    void unsigned_number(std::uint32_t value) override{output+=std::to_string(value);}
    void decimal(double value,unsigned digits) override{
        // SDK Print formatting boundary; target adapter calls Client::print.
        if(std::isnan(value)){output+="nan";return;}if(std::isinf(value)){output+="inf";return;}
        if(value>4294967040.0 || value< -4294967040.0){output+="ovf";return;}
        if(value<0){output+='-';value=-value;}double round=.5;for(unsigned i=0;i<digits;++i)round/=10;value+=round;
        const auto integer=static_cast<std::uint32_t>(value);unsigned_number(integer);double rest=value-integer;if(digits)output+='.';
        for(unsigned i=0;i<digits;++i){rest*=10;const auto digit=static_cast<std::uint32_t>(rest);unsigned_number(digit);rest-=digit;}
    }
} stream;
struct Listener final:HttpListener {
    void listen(unsigned) override{}
    HttpStream* accept() override{ports.events.push_back("accept");return &stream;}
    void release(HttpStream&) override{ports.events.push_back("release");}
} listener;
struct Device final:HttpDevice {
    const std::uint32_t* flags=nullptr;const char* const* strings=nullptr;int scan=0;const std::uint8_t* factory=nullptr;
    void event(const char* name){ports.events.push_back(std::string("device:")+name);}
    const char* version() override{event("version");return strings[0];}
    const char* ssid() override{event("ssid");return strings[1];}
    bool has_ssid() override{event("has_ssid");return flags[0];}
    bool has_station_ip() override{event("has_station_ip");return flags[1];}
    std::uint32_t station_ip() override{event("station_ip");return flags[7];}
    bool needs_update() override{event("needs_update");return flags[2];}
    bool is_program_running() override{event("is_program_running");return flags[3];}
    bool wifi_connected() override{event("wifi_connected");return flags[4];}
    bool sensors_enabled() override{event("sensors_enabled");return flags[5];}
    bool fan_enabled() override{event("fan_enabled");return flags[6];}
    const char* program_name() override{event("program_name");return strings[2];}
    const char* sensor_csv() override{event("sensor_csv");return strings[3];}
    const char* hardware_error() override{event("hardware_error");return strings[4];}
    void configure(const char* ssid,const char* password) override{ports.events.push_back(std::string("device:configure:")+ssid+":"+password);}
    void connect_to_lan() override{event("connect_to_lan");}
    void start_program(ProgramId program) override{ports.events.push_back("device:start:"+std::to_string(static_cast<unsigned>(program)));}
    void stop_program() override{event("stop_program");}
    void set_fan(bool value) override{ports.events.push_back("device:fan:"+std::to_string(value));}
    void set_pump(bool value) override{ports.events.push_back("device:pump:"+std::to_string(value));}
    void set_sensors(bool value) override{ports.events.push_back("device:sensors:"+std::to_string(value));}
    void factory_result(FactoryResult& result) override{event("factory_result");std::memcpy(&result.test,factory,56);std::memcpy(result.pressure.data(),factory+56,800);result.pressure_count=read(factory,856);}
    void factory_reset() override{event("factory_reset");}
    int scan_count() override{event("scan_count");return scan;}
    const char* scan_ssid(unsigned index) override{ports.events.push_back("device:scan_ssid:"+std::to_string(index));return strings[5+index];}
} device;
std::string request_hex,ssid_hex,password_hex,error_text;
}
extern "C" {
void server_run(const char* request,const std::uint32_t* flags,const char* const* strings,int scan,const std::uint8_t* factory,unsigned now,unsigned tick){
    ports.events.clear();ports.clock=now;diagnostics.assertion_count=0;runtime.tick=tick;stream.position=0;stream.input=request;stream.output.clear();device.flags=flags;device.strings=strings;device.scan=scan;device.factory=factory;
    HttpServer server(listener,runtime,diagnostics,device);server.poll();std::array<std::uint8_t,0x304> raw={{0}};
    std::memcpy(raw.data(),server.request.uri.data(),256);write(raw.data(),0x100,static_cast<std::uint32_t>(server.request.content_length));std::memcpy(raw.data()+0x104,server.request.body.data(),512);
    request_hex=hex(raw.data(),raw.size());ssid_hex=hex(server.form.ssid.data(),server.form.ssid.size());password_hex=hex(server.form.password.data(),server.form.password.size());error_text=server.request.error?server.request.error:"";
}
const char* server_bytes(unsigned i){return i==0?request_hex.c_str():i==1?ssid_hex.c_str():password_hex.c_str();}
const char* server_output(){return stream.output.c_str();}
const char* server_error(){return error_text.c_str();}
}
