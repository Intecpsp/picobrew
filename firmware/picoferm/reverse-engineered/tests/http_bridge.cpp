#include "picoferm/http_server.hpp"
#include "formatter.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
using namespace picoferm::oem;
namespace {
std::vector<std::string> events,fields;
std::string number(std::uint32_t value){return std::to_string(value);}
std::string hex(const void* memory,std::size_t size){const auto* data=static_cast<const unsigned char*>(memory);std::string result;static const char digits[]="0123456789abcdef";for(std::size_t i=0;i<size;++i){result+=digits[data[i]>>4];result+=digits[data[i]&15];}return result;}
struct Boundary final:Ports,HardwarePorts,Runtime,HttpStream,HttpListener,HttpDevice {
    std::uint32_t* words;std::uint32_t now;std::size_t position=0;unsigned available_calls=0;std::string wire,response,scan;
    DeviceData data;PersistentData stored;bool changed=false;
    Boundary(std::uint32_t* input,const char* bytes,std::size_t length,const std::uint8_t* record):words(input),now(input[1]),wire(bytes,length){std::memcpy(stored.bytes.data(),record,236);std::strcpy(data.version.data(),"0.2.7");std::strcpy(data.website.data(),"example.test");std::strcpy(data.uid.data(),"001122aabbcc");}
    void yield() override{events.push_back("yield");}
    void text(const char* scope,const char* key,const char* value) override{events.push_back(std::string("text:")+scope+":"+key+":"+value);}
    void masked_text(const char* scope,const char* key,const char* value) override{const std::string mask(std::strlen(value),'*');text(scope,key,mask.c_str());}
    void integer(const char* scope,const char* key,std::uint32_t value) override{events.push_back(std::string("integer:")+scope+":"+key+":"+::number(value));}
    void number(const char*,const char*,float) override{}
    void boolean(const char* scope,const char* key,bool value) override{events.push_back(std::string("boolean:")+scope+":"+key+":"+(value?"1":"0"));}
    void digital_write(unsigned,bool) override{}std::uint32_t digital_read(unsigned) override{return 0;}void persist(const std::uint8_t*,std::size_t) override{}
    std::uint32_t millis() override{const auto value=now;events.push_back("millis:"+::number(value));now+=words[2];return value;}
    void pin_mode(unsigned,unsigned) override{}std::int32_t analog_read(unsigned) override{return 0;}void wire_begin(unsigned,unsigned) override{}
    void wire_begin_transmission(unsigned) override{}void wire_write(std::uint8_t) override{}void wire_end_transmission() override{}void wire_request(unsigned,unsigned) override{}std::uint32_t wire_read() override{return 0;}
    void delay_ms(std::uint32_t value) override{events.push_back("delay:"+::number(value));now+=value;}
    bool connected() override{const bool result=position<words[4]&&(words[5]||position<wire.size());events.push_back(std::string("connected:")+(result?"1":"0"));return result;}
    int available() override{++available_calls;const int result=words[3]&&available_calls%words[3]==0?0:static_cast<int>(wire.size()-position);events.push_back("available:"+std::to_string(result));return result;}
    int read() override{const int result=position<wire.size()?static_cast<unsigned char>(wire[position++]):-1;events.push_back("read:"+std::to_string(result));return result;}
    std::size_t write(const char* bytes,std::size_t length) override{response.append(bytes,length);return length;}
    void flush() override{events.push_back("flush:"+std::to_string(response.size()));}
    void listen(unsigned port) override{events.push_back("listen:"+::number(port));}
    void process_dns() override{events.push_back("dns");}
    HttpStream* accept() override{events.push_back("accept");return words[7]?this:nullptr;}
    void release(HttpStream&) override{events.push_back("release");}
    CalibrationStatus calibration() override{events.push_back("calibration");CalibrationStatus result;result.error=words[12]!=0;result.can_calibrate=words[13]!=0;result.message=words[14]?"Pressure requires calibration":"";return result;}
    void configure(char* result,std::size_t capacity,const char* ssid,const char* password) override{configure_credentials(data,stored,ssid,password,result,capacity,changed,*this);}
    void factory_reset() override{events.push_back("factory_reset");}void calibrate() override{events.push_back("calibrate");}
    int scan_count() override{events.push_back("scan_count:"+std::to_string(static_cast<std::int32_t>(words[11])));return static_cast<std::int32_t>(words[11]);}
    const char* scan_ssid(unsigned index) override{events.push_back("scan_ssid:"+::number(index));scan=index==0?"Home Wi-Fi":index==1?"<raw&SSID>":index==2?"":"network-"+::number(index);return scan.c_str();}
};
}
extern "C" std::uint32_t ferm_http_run(unsigned op,std::uint32_t* words,const char* first_input,const char* second_input,const char* body,const char* wire,std::uint32_t wire_length,const std::uint8_t* record){
    events.clear();fields.clear();Boundary boundary(words,wire,wire_length,record);FakeFormatter formatter;
    HttpServer server(boundary,boundary,boundary,boundary,formatter,boundary.data,boundary.stored,boundary,words[0]);server.enabled=words[6]!=0;
    auto& request=server.request;std::strcpy(request.uri.data(),"previous-uri");std::memcpy(request.body.data(),body,std::min(std::strlen(body),request.body.size()-1));request.content_length=static_cast<std::int32_t>(words[10]);request.headers_complete=words[9]!=0;request.error=words[8]?"prior error":nullptr;
    std::strcpy(server.form.ssid.data(),"previous-ssid");std::strcpy(server.form.password.data(),"previous-pass");std::strcpy(server.trigger.data(),"old");
    unsigned char output[128];std::memset(output,0xa5,sizeof(output));std::string first_storage=first_input;
    const char* first=words[17]?nullptr:first_storage.c_str();const char* second=words[18]?nullptr:second_input;std::uint32_t result=0;
    switch(op){
    case 0:result=string_equal(first,second);break;case 1:result=string_starts_with(first,second);break;
    case 2:{char* found=find_character(const_cast<char*>(first),static_cast<char>(words[16]));result=found?static_cast<std::uint32_t>(found-first):0xffffffffu;break;}
    case 3:url_decode(reinterpret_cast<char*>(output),words[15],first_input,boundary);break;
    case 4:parse_request_line(first_input,request,boundary);break;
    case 5:parse_http(boundary,request,boundary,boundary,words[0]);break;
    case 6:parse_wifi_form(request,server.form,boundary);break;
    case 7:parse_trigger(request,server.trigger,second,boundary);break;
    case 8:result=server.validate_trigger(boundary,request,second);break;
    case 9:server.page_header(boundary);break;case 10:server.setup_page(boundary);break;
    case 11:server.reset_page(boundary);break;case 12:server.calibration_page(boundary);break;case 13:server.details_page(boundary);break;
    case 14:server.configure_page(boundary,request);break;case 15:server.poll();break;
    case 16:run_cstring_self_tests(boundary);break;case 17:run_http_parser_self_tests(boundary);break;
    }
    fields.push_back(hex(request.uri.data(),256));fields.push_back(hex(request.body.data(),512));fields.push_back(request.error?request.error:"");
    fields.push_back(hex(server.form.ssid.data(),48)+hex(server.form.password.data(),64));fields.push_back(hex(server.trigger.data(),10));fields.push_back(hex(output,128));fields.push_back(boundary.response);fields.push_back(hex(boundary.stored.bytes.data(),236));
    fields.push_back(hex(boundary.data.version.data(),16)+hex(boundary.data.website.data(),32)+hex(boundary.data.uid.data(),16)+hex(boundary.data.ap_ssid.data(),32)+hex(boundary.data.token.data(),16));
    words[0]=static_cast<std::uint32_t>(request.content_length);words[1]=request.headers_complete;words[2]=boundary.changed;words[3]=boundary.now;words[4]=static_cast<std::uint32_t>(boundary.position);return result;
}
extern "C" const char* ferm_http_field(unsigned index){return fields.at(index).c_str();}
extern "C" unsigned ferm_http_event_count(){return static_cast<unsigned>(events.size());}
extern "C" const char* ferm_http_event(unsigned index){return events.at(index).c_str();}
