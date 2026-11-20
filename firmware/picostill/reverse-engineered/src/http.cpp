#include "picostill/http.hpp"
#include <cstring>
#include <cstdlib>
#include <limits>

namespace picostill { namespace oem {
bool string_equal(const char* first,const char* second) noexcept {
    if(first==second)return true;
    return first && second && std::strcmp(first,second)==0;
}
bool string_starts_with(const char* text,const char* prefix) noexcept {
    if(text==prefix || !prefix)return true;
    if(!text)return !*prefix;
    while(*text && *text==*prefix){++text;++prefix;}
    return !*prefix;
}
char* find_character(char* text,char value) noexcept {
    if(text)while(*text){if(*text==value)return text;++text;}
    return nullptr; // OEM does not return the terminator when value is zero.
}
bool string_boolean(const char* text) noexcept {
    return text && !string_equal(text,"0") && !string_equal(text,"off") && !string_equal(text,"false");
}
bool url_decode(char* destination,std::size_t capacity,const char* encoded,Diagnostics& diagnostics) {
    diagnostics.require(destination!=nullptr,"CString.h",0x4a,"dest != 0");
    diagnostics.require(capacity!=1,"CString.h",0x4b,"destsize > 0");
    // Original size zero/null accesses are outside the bounded string domain.
    if(!destination || !capacity || !encoded)return false;
    std::size_t output=0;const auto size=std::strlen(encoded);
    for(std::size_t input=0;input<size && output<capacity-1;++output){
        char value=encoded[input++];
        if(value=='+')value=' ';
        else if(value=='%' && input+1<size){char number[5]={'0','x',encoded[input],encoded[input+1],0};value=static_cast<char>(std::strtol(number,nullptr,16));input+=2;}
        destination[output]=value;
    }
    destination[output]=0;return true;
}
void parse_request_line(const char* line,HttpRequest& request,HttpRuntime& runtime) {
    const char* first=std::strchr(line,' ');
    if(!first){request.fail("Request-Line has no space");return;}
    ++first;const char* end=std::strchr(first,' ');
    if(!end || end==first){request.fail("Request-Line has no Request-URI");return;}
    const auto count=static_cast<std::size_t>(end-first);
    if(count>=request.uri.size()){request.fail("Request-URI exceeds buffer");return;} // Owned overflow guard.
    std::memcpy(request.uri.data(),first,count);request.uri[count]=0;runtime.variable("HTTP","URI",request.uri.data());
}
static std::int32_t decimal32(const char* text) {
    const auto value=::strtoll(text,nullptr,10);
    if(value>std::numeric_limits<std::int32_t>::max())return std::numeric_limits<std::int32_t>::max();
    if(value<std::numeric_limits<std::int32_t>::min())return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}
void parse_http(HttpStream& stream,HttpRequest& request,HttpRuntime& runtime,std::uint32_t timeout) {
    runtime.variable("HTTP","HttpParser::Parser","BEGIN");request.uri.fill(0);request.body.fill(0);request.content_length=-1;request.error=nullptr;
    std::array<char,512> line={{0}};std::size_t line_length=0,body_length=0;std::uint32_t remaining=0,deadline=runtime.millis()+timeout;
    bool headers=true,request_seen=false;
    while(stream.connected()) {
        runtime.cooperate();if(!headers && (remaining==0 || remaining>=0x80000000u))break;
        const auto now=runtime.millis();const int count=stream.available();
        if(count<1){if(now>deadline){request.fail("Client not available (timed out)");break;}continue;}
        deadline=now+timeout;const auto value=static_cast<std::uint32_t>(stream.read());
        if(value>255){runtime.integer("HTTP","client.read()",value);runtime.integer("HTTP","available",static_cast<std::uint32_t>(count));continue;}
        if(!headers){request.body[body_length]=static_cast<char>(value);if(body_length<511)++body_length;--remaining;}
        else if(value==10){
            if(!line_length)headers=false;
            else if(request_seen){
                runtime.variable("HTTP","HEADER",line.data());
                if(string_starts_with(line.data(),"Content-Length: ")){runtime.variable("HTTP","CLEN",line.data()+16);request.content_length=decimal32(line.data()+16);remaining=static_cast<std::uint32_t>(request.content_length);}
            } else {runtime.variable("HTTP","REQUEST",line.data());parse_request_line(line.data(),request,runtime);request_seen=true;}
            line_length=0;line[0]=0;
        } else if(value!=13){
            if(line_length<511){line[line_length++]=static_cast<char>(value);line[line_length]=0;}
            else runtime.message("HttpBuffer is too small!");
        }
    }
    if(headers)request.fail("Parse interrupted before end of headers");
    runtime.variable("HTTP","HttpParser::Parser","END");
}
void parse_wifi_form(HttpRequest& request,WifiForm& form,Diagnostics& diagnostics) {
    if(!std::memchr(request.body.data(),0,request.body.size())){request.fail("Unterminated FORM data");return;} // Owned scan bound.
    char* separator=find_character(request.body.data(),'&');char* equals=separator?find_character(separator+1,'='):nullptr;
    if(!separator || !equals || !string_starts_with(request.body.data(),"ssid=")){request.fail("Invalid FORM data");return;}
    *separator=0;url_decode(form.ssid.data(),form.ssid.size(),request.body.data()+5,diagnostics);url_decode(form.password.data(),form.password.size(),equals+1,diagnostics);
    if(!form.ssid[0])request.fail("Missing SSID variable");
}
}} // namespace picostill::oem
