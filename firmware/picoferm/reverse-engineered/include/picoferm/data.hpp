#pragma once
#include "picoferm/core.hpp"
#include <cstring>
namespace picoferm { namespace oem {
// Returning non-null string domain of OEM 0x4020348c. Invalid capacity/null
// arguments are explicitly rejected instead of reproducing invalid writes.
bool bounded_copy(char* destination,std::size_t capacity,const char* source,Ports& ports);
bool encode_hex(char* destination,std::size_t capacity,const std::uint8_t* bytes,std::size_t length,Ports& ports); // 0x402043c4
template<std::size_t Size> struct Buffer {
    std::array<char,Size> bytes={{0}};std::size_t length=0,left=Size-1;
    void clear(){bytes.fill(0);length=0;left=Size-1;}
    void append(const char* data,std::size_t size,Ports& ports){
        if(!data||!size)return;
        if(size>=left)diagnostic_assertion(ports);
        if(size>left)size=left;
        if(size)std::memcpy(bytes.data()+length,data,size);
        length+=size;left-=size;
    }
    void append(const char* text,Ports& ports){if(text)append(text,std::strlen(text),ports);}
    const char* c_str() const noexcept{return bytes.data();}
};
struct DeviceData {
    std::array<char,16> version={{0}};
    std::array<char,32> website={{0}};
    std::array<char,16> uid={{0}};
    std::array<char,32> ap_ssid={{0}};
    std::array<char,16> token={{0}};
    void initialize(const char* version_text,const char* website_text,Ports& ports); // 0x40204578
    void set_mac(const std::uint8_t* bytes,std::size_t length,Ports& ports); // 0x40204440
    void update_token(const char* ssid,const char* password,Ports& ports); // 0x40204480
};
struct Formatter {
    virtual ~Formatter()=default;
    // Original SDK dtostrf(value,4,2). Destination is deliberately larger than
    // OEM temporaries so an extreme measurement cannot overwrite the stack.
    virtual void decimal(float value,char* destination,std::size_t capacity)=0;
};
const char* session_name(SessionState state) noexcept;
void report_storage(const PersistentData& data,Ports& ports,Formatter& formatter); // 0x40203bbc
// Caller retains the stable EEPROM storage. Mutates only SSID/password,
// has-WiFi, the Data token, and the application credentials_changed flag.
void configure_credentials(DeviceData& data,PersistentData& stored,const char* ssid,const char* password,
                           char* result,std::size_t capacity,bool& credentials_changed,Ports& ports); // 0x402044e8
}}
