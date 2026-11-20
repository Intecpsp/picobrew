#include "picostill/data.hpp"
#include <cstring>
#include <stdio.h>

namespace picostill { namespace oem {
bool bounded_copy(char* destination,std::size_t capacity,const char* source,Diagnostics& diagnostics) {
    // OEM reads strlen first. Null pointers are an owned rejection domain.
    if(!destination || !source)return false;
    const std::size_t length=std::strlen(source);
    diagnostics.require(capacity>0,"CString.h",0x3f,"destsize > 0");
    diagnostics.require(capacity>0 && length<capacity,"CString.h",0x40,"srclen < destsize");
    if(capacity==0)return false;
    const std::size_t copied=length<capacity-1?length:capacity-1;
    std::memcpy(destination,source,copied);destination[copied]=0;return true;
}
char hex_digit(std::uint8_t value,Diagnostics& diagnostics) {
    diagnostics.require(value<16,"Data.h",0x26,"val < 16");
    return static_cast<char>(value+(value<10?0x30:0x57));
}
bool encode_hex(char* destination,std::size_t capacity,const std::uint8_t* source,std::size_t count,Diagnostics& diagnostics) {
    diagnostics.require(capacity>0,"Data.h",0x2e,"bufsize > 0");
    const bool fits=capacity>0 && count<=(capacity-1)/2;
    diagnostics.require(fits,"Data.h",0x2f,"count * 2 < bufsize");
    // OEM overruns an insufficient buffer after its assertions return.
    if(!destination || (!source && count) || !fits)return false;
    for(std::size_t i=0;i<count;++i){destination[2*i]=hex_digit(source[i]>>4,diagnostics);destination[2*i+1]=hex_digit(source[i]&15,diagnostics);}
    destination[count*2]=0;return true;
}
void DeviceData::update_token(Diagnostics& diagnostics) {
    std::array<std::uint8_t,4> value={{0,0,0,0}};unsigned position=0;
    for(const char* text:{ssid.data(),password.data()})for(;*text;++text){value[position]^=static_cast<std::uint8_t>(*text);position=(position+1)&3;}
    encode_hex(token.data(),token.size(),value.data(),value.size(),diagnostics);
}
void DeviceData::set_ssid(const char* value,Diagnostics& diagnostics){bounded_copy(ssid.data(),ssid.size(),value,diagnostics);update_token(diagnostics);}
void DeviceData::set_password(const char* value,Diagnostics& diagnostics){bounded_copy(password.data(),password.size(),value,diagnostics);update_token(diagnostics);}
void DeviceData::set_uid(const char* mac,Diagnostics& diagnostics) {
    if(!mac)return;
    diagnostics.require(std::strlen(mac)<=uid.size(),"Data.h",0x6d,"strlen(mac) <= sizeof(_uid)");
    // OEM passes the MAC as snprintf's format. Its startup supplies twelve
    // hexadecimal characters; literal copying also defines safe console input.
    snprintf(uid.data(),uid.size(),"%s",mac);
    bounded_copy(ap_ssid.data(),ap_ssid.size(),"PicoStill_",diagnostics);
    bounded_copy(ap_ssid.data()+10,ap_ssid.size()-10,uid.data(),diagnostics);
}
void DeviceData::initialize(const char* firmware,const char* host,Diagnostics& diagnostics) {
    diagnostics.require(firmware && *firmware,"Data.h",0x53,"version != 0 && strlen(version) > 0");
    diagnostics.require(host && *host,"Data.h",0x54,"website != 0 && strlen(website) > 0");
    version.fill(0);website.fill(0);uid.fill(0);ap_ssid.fill(0);ssid.fill(0);password.fill(0);token.fill(0);
    bounded_copy(version.data(),version.size(),firmware,diagnostics);bounded_copy(website.data(),website.size(),host,diagnostics);update_token(diagnostics);
}
}} // namespace picostill::oem
