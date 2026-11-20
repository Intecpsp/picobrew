#include "picoferm/http.hpp"
#include <cstdlib>
#include <cstring>
#include <limits>

namespace picoferm { namespace oem {
bool string_equal(const char* first,const char* second) noexcept {
    return first==second || (first&&second&&std::strcmp(first,second)==0);
}
bool string_starts_with(const char* text,const char* prefix) noexcept {
    if(text==prefix||!prefix)return true;
    if(!text)return !*prefix;
    while(*text&&*text==*prefix){++text;++prefix;}
    return !*prefix;
}
char* find_character(char* text,char value) noexcept {
    if(text)for(;*text;++text)if(*text==value)return text;
    return nullptr;
}
bool url_decode(char* destination,std::size_t capacity,const char* encoded,Ports& ports){
    if(!destination||!encoded||!capacity){diagnostic_assertion(ports);return false;}
    if(capacity==1)diagnostic_assertion(ports);
    const auto length=std::strlen(encoded);std::size_t written=0;
    for(std::size_t index=0;written<capacity-1&&index<length;){
        unsigned char value=static_cast<unsigned char>(encoded[index++]);
        if(value=='+')value=' ';
        else if(value=='%'&&index+1<length){
            char number[5]={'0','x',encoded[index],encoded[index+1],0};
            value=static_cast<unsigned char>(std::strtol(number,nullptr,16));index+=2;
        }
        destination[written++]=static_cast<char>(value);
    }
    destination[written]=0;return true;
}
void parse_request_line(const char* line,HttpRequest& request,Ports& ports){
    const char* first=line?std::strchr(line,' '):nullptr;
    if(!first){request.fail("Request-Line has no space");return;}
    const char* begin=first+1;const char* end=std::strchr(begin,' ');
    if(!end||end<=begin){request.fail("Request-Line has no Request-URI");return;}
    const auto length=static_cast<std::size_t>(end-begin);
    if(length>=request.uri.size()){request.fail("Request URI too long");return;}
    std::memcpy(request.uri.data(),begin,length);request.uri[length]=0;
    ports.text("HTTP","URI",request.uri.data());
}
static std::int32_t decimal_int32(const char* text){
    // The OEM newlib long is signed 32-bit and saturates on overflow.
    const long long value=::strtoll(text,nullptr,10);
    if(value>std::numeric_limits<std::int32_t>::max())return std::numeric_limits<std::int32_t>::max();
    if(value<std::numeric_limits<std::int32_t>::min())return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}
void parse_http(HttpStream& stream,HttpRequest& request,HardwarePorts& hardware,Ports& ports,std::uint32_t timeout){
    request=HttpRequest();std::array<char,512> line={{0}};
    std::size_t line_length=0,body_index=0;bool first_line=false,headers=true;
    std::uint32_t remaining=0,deadline=hardware.millis()+timeout;
    while(stream.connected()){
        ports.yield();
        if(!headers&&((remaining|(remaining-1))&0x80000000u))break;
        const auto now=hardware.millis();
        if(stream.available()!=0){
            deadline=now+timeout;const auto value=static_cast<unsigned char>(stream.read());
            if(!headers){
                if(body_index<request.body.size()-1)request.body[body_index++]=static_cast<char>(value);
                else request.fail("Request body too long");
                --remaining;
            }else if(value=='\n'){
                if(line_length==0)headers=false;
                else if(!first_line){ports.text("HTTP","REQUEST",line.data());parse_request_line(line.data(),request,ports);first_line=true;}
                else if(string_starts_with(line.data(),"Content-Length: ")){
                    ports.text("HTTP","CLEN",line.data()+16);request.content_length=decimal_int32(line.data()+16);
                    remaining=static_cast<std::uint32_t>(request.content_length);
                }
                line_length=0;line[0]=0;
            }else if(value!='\r'){
                if(line_length<line.size()-1){line[line_length++]=static_cast<char>(value);line[line_length]=0;}
                else diagnostic_warning(ports);
            }
        }else if(now>deadline){request.fail("Client not available (timed out)");break;}
    }
    request.headers_complete=!headers;
}
void parse_wifi_form(HttpRequest& request,WifiForm& form,Ports& ports){
    char* body=request.body.data();char* separator=find_character(body,'&');
    char* password=separator?find_character(separator+1,'='):nullptr;
    ports.text("SRV","CONTENT",body);
    if(!separator||!password||!string_starts_with(body,"ssid=")){request.fail("Invalid FORM data");return;}
    *separator=0;url_decode(form.ssid.data(),form.ssid.size(),body+5,ports);
    url_decode(form.password.data(),form.password.size(),password+1,ports);
    if(!form.ssid[0])request.fail("Missing SSID variable");
}
void parse_trigger(HttpRequest& request,std::array<char,10>& trigger,const char* expected,Ports& ports){
    ports.text("SRV","CONTENT",request.body.data());
    if(!expected)diagnostic_assertion(ports);
    if(!string_starts_with(request.body.data(),"trigger=")){request.fail("Invalid FORM data");return;}
    url_decode(trigger.data(),trigger.size(),request.body.data()+8,ports);
    for(char* cursor=trigger.data();*cursor;++cursor)if(*cursor>='A'&&*cursor<='Z')*cursor=static_cast<char>(*cursor+32);
    if(!trigger[0]||!string_equal(expected,trigger.data()))request.fail("Please type the action you want to perform in the input field.");
}
void run_cstring_self_tests(Ports& ports){
    const auto check=[&ports](bool success){if(!success)diagnostic_assertion(ports);};
    check(!string_equal("a","A"));check(string_starts_with("Some random long text","Some rand"));
    check(!string_starts_with("Some random long text","Some partial prefix"));
    check(string_starts_with("ab","a"));check(!string_starts_with("a","ab"));
    check(string_starts_with("a",""));check(!string_starts_with("","a"));
    char alphabet[]="abc";check(find_character(alphabet,'a')==alphabet);check(find_character(alphabet,'b')==alphabet+1);
    check(!find_character(alphabet,'0'));char output[64];
    bounded_copy(output,16,"Basic copy test",ports);check(string_equal(output,"Basic copy test"));
    ports.text("TEST","ExpectedError","ERR ASSERT srclen < destsize");
    bounded_copy(output,5,"Long string that is longer than actual dest buffer size",ports);check(string_equal(output,"Long"));
    static const char* const decoded[][2]={
        {"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789","abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"},
        {"a+b+c+d+e+f","a b c d e f"},{"a%20b%20c%20d%20e%20f","a b c d e f"},
        {"%60%21%40%23%24%25%5E%26%2A%28%29_%2B%7B%7D%7C%5B%5D%5C%3A%22%3B%27%3C%3E%3F%2C.%2F%2A-%2B.","`!@#$%^&*()_+{}|[]\\:\";'<>?,./*-+."}
    };
    for(const auto& pair:decoded){url_decode(output,sizeof(output),pair[0],ports);check(string_equal(output,pair[1]));}
}
void run_http_parser_self_tests(Ports& ports){
    const auto check=[&ports](bool success){if(!success)diagnostic_assertion(ports);};
    HttpRequest request;parse_request_line("GET / HTTP/1.0",request,ports);check(string_equal(request.uri.data(),"/"));
    parse_request_line("GET /foo HTTP/1.0",request,ports);check(string_equal(request.uri.data(),"/foo"));
    parse_request_line("Invalid_request_line_without_space",request,ports);check(request.error!=nullptr);
    parse_request_line("method uri_not_followed_by_a_space",request,ports);check(request.error!=nullptr);
}
void run_http_self_tests(Ports& ports){run_cstring_self_tests(ports);run_http_parser_self_tests(ports);}
}}
