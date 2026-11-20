#include "controller_bridge.cpp"
#include "picostill/http.hpp"
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
    const int* items=nullptr;unsigned count=0,index=0;bool keep=false;
    bool connected() override{bool result=keep||index<count;ports.events.push_back("connected:"+std::to_string(result));return result;}
    int available() override{int result=index<count?1:0;ports.events.push_back("available:"+std::to_string(result));return result;}
    int read() override{int result=items[index++];ports.events.push_back("read:"+std::to_string(result));return result;}
    std::size_t write(const char*,std::size_t) override{return 0;}
    void flush() override{}
    void stop() override{}
    void decimal(double,unsigned) override{}
    void unsigned_number(std::uint32_t) override{}
} stream;
std::string request_hex,ssid_hex,password_hex,decoded_hex,error_text;unsigned http_result;
}
extern "C" {
void http_run(unsigned op,const char* text,const char* other,unsigned capacity,unsigned now,unsigned tick,int keep,const char* error,
              const int* items,unsigned count,unsigned timeout){
    ports.events.clear();ports.clock=now;diagnostics.assertion_count=0;runtime.tick=tick;stream.items=items;stream.count=count;stream.index=0;stream.keep=keep;
    HttpRequest request;request.error=error&&*error?error:nullptr;WifiForm form;std::array<char,128> decoded;decoded.fill(static_cast<char>(0xcc));http_result=0;
    if(op==0)parse_http(stream,request,runtime,timeout);else if(op==1)parse_request_line(text,request,runtime);
    else if(op==2){std::memcpy(request.body.data(),text,std::min(std::strlen(text),request.body.size()));parse_wifi_form(request,form,diagnostics);}
    else if(op==3){url_decode(decoded.data(),capacity,text,diagnostics);}
    else if(op==4)http_result=string_boolean(text);else if(op==5)http_result=string_equal(text,other);else if(op==6)http_result=string_starts_with(text,other);
    std::array<std::uint8_t,0x304> raw={{0}};std::memcpy(raw.data(),request.uri.data(),256);write(raw.data(),0x100,static_cast<std::uint32_t>(request.content_length));std::memcpy(raw.data()+0x104,request.body.data(),512);
    request_hex=hex(raw.data(),raw.size());ssid_hex=hex(form.ssid.data(),form.ssid.size());password_hex=hex(form.password.data(),form.password.size());decoded_hex=hex(decoded.data(),decoded.size());error_text=request.error?request.error:"";
}
const char* http_bytes(unsigned i){return i==0?request_hex.c_str():i==1?ssid_hex.c_str():i==2?password_hex.c_str():decoded_hex.c_str();}
const char* http_error(){return error_text.c_str();}
unsigned http_value(){return http_result;}
}
