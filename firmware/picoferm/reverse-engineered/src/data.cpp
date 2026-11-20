#include "picoferm/data.hpp"
#include <stdio.h>
#include <limits>
namespace picoferm { namespace oem {
bool bounded_copy(char* destination,std::size_t capacity,const char* source,Ports& ports){
    if(!source||!destination){diagnostic_assertion(ports);return false;}
    const auto length=std::strlen(source);
    if(capacity==0){diagnostic_assertion(ports);diagnostic_assertion(ports);return false;}
    if(length>=capacity)diagnostic_assertion(ports);
    const auto size=length<capacity?length:capacity-1;
    if(size)std::memcpy(destination,source,size);
    destination[size]=0;return true;
}
bool encode_hex(char* destination,std::size_t capacity,const std::uint8_t* bytes,std::size_t length,Ports& ports){
    if(capacity==0)diagnostic_assertion(ports);
    if(length>std::numeric_limits<std::size_t>::max()/2||capacity<=length*2){diagnostic_assertion(ports);return false;}
    if(!destination||(!bytes&&length)){diagnostic_assertion(ports);return false;}
    static const char digits[]="0123456789abcdef";
    for(std::size_t i=0;i<length;++i){destination[2*i]=digits[bytes[i]>>4];destination[2*i+1]=digits[bytes[i]&15];}
    destination[length*2]=0;return true;
}
void DeviceData::initialize(const char* version_text,const char* website_text,Ports& ports){
    if(!version_text||!*version_text)diagnostic_assertion(ports);
    if(!website_text||!*website_text)diagnostic_assertion(ports);
    version.fill(0);website.fill(0);uid.fill(0);ap_ssid.fill(0);token.fill(0);
    bounded_copy(version.data(),version.size(),version_text?version_text:"",ports);
    bounded_copy(website.data(),website.size(),website_text?website_text:"",ports);update_token("","",ports);
}
void DeviceData::set_mac(const std::uint8_t* bytes,std::size_t length,Ports& ports){
    if(!encode_hex(uid.data(),uid.size(),bytes,length,ports))return;
    bounded_copy(ap_ssid.data(),ap_ssid.size(),"PicoFerm_",ports);bounded_copy(ap_ssid.data()+9,ap_ssid.size()-9,uid.data(),ports);
}
void DeviceData::update_token(const char* ssid,const char* password,Ports& ports){
    std::uint8_t bytes[4]={0,0,0,0};unsigned index=0;
    for(const char* source:{ssid,password}){
        if(!source){diagnostic_assertion(ports);continue;}
        while(*source){bytes[index]^=static_cast<std::uint8_t>(*source++);index=(index+1)&3;}
    }
    encode_hex(token.data(),token.size(),bytes,4,ports);
}
const char* session_name(SessionState state) noexcept {static const char* const labels[]={"Undefined","Idle","Fermenting"};const auto index=static_cast<unsigned>(state);return index<3?labels[index]:"SessionState::INVALID";}
void report_storage(const PersistentData& data,Ports& ports,Formatter& formatter){
    char ssid[33]={0},password[65]={0};std::memcpy(ssid,data.bytes.data(),32);std::memcpy(password,data.bytes.data()+32,64);
    ports.text("EPRM","Ssid",ssid);ports.masked_text("EPRM","Password",password);
    ports.boolean("EPRM","HasWifi",data.bytes[0x61]==1);ports.integer("EPRM","SessionState",data.bytes[0x62]);ports.integer("EPRM","Button",data.bytes[0x63]);
    ports.number("EPRM","PressureOffset",static_cast<float>(data.word(PersistentData::pressure_offset_position)));
    ports.boolean("EPRM","HasPressureOffset",data.bytes[PersistentData::has_offset_position]!=0);
    ports.integer("EPRM","PressureSlope",data.word(PersistentData::slope_position));ports.integer("EPRM","Version",data.word(PersistentData::version_position));
    ports.integer("EPRM","LogStartIndex",data.bytes[PersistentData::log_start_position]);ports.integer("EPRM","NumLogs",data.bytes[PersistentData::log_count_position]);
    for(LogIterator iterator(data);!iterator.done();iterator.advance()){
        Measurement sample;if(!data.measurement(iterator.index,sample)){diagnostic_assertion(ports);return;}
        char temperature[48],pressure[48],line[128];formatter.decimal(sample.temperature,temperature,sizeof(temperature));formatter.decimal(sample.pressure,pressure,sizeof(pressure));
        snprintf(line,sizeof(line),"[%u %s %s]",iterator.index,temperature,pressure);ports.text("EPRM","Data",line);
    }
}
void configure_credentials(DeviceData& data,PersistentData& stored,const char* ssid,const char* password,char* result,std::size_t capacity,bool& credentials_changed,Ports& ports){
    ports.text("MAIN","FUNC","Configure");ports.text("WEB","SSID",ssid?ssid:"");ports.masked_text("WEB","PASS",password?password:"");
    bounded_copy(reinterpret_cast<char*>(stored.bytes.data()),32,ssid?ssid:"",ports);
    bounded_copy(reinterpret_cast<char*>(stored.bytes.data()+32),64,password?password:"",ports);
    data.update_token(ssid,password,ports);bounded_copy(result,capacity,data.token.data(),ports);stored.bytes[0x61]=0;credentials_changed=true;
}
}}
