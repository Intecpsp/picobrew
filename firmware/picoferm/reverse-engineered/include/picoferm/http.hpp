#pragma once
#include "picoferm/data.hpp"
#include "picoferm/hardware.hpp"

namespace picoferm { namespace oem {
struct HttpStream {
    virtual ~HttpStream()=default;
    virtual bool connected()=0;
    virtual int available()=0;
    virtual int read()=0;
    virtual std::size_t write(const char* bytes,std::size_t length)=0;
    // ESP8266 flash requires the SDK's aligned flash-reading implementation.
    virtual std::size_t write_flash(const char* bytes,std::size_t length){return write(bytes,length);}
    virtual void flush()=0;
};
struct HttpListener {
    virtual ~HttpListener()=default;
    virtual void listen(unsigned port)=0;
    virtual void process_dns()=0;
    virtual HttpStream* accept()=0;
    virtual void release(HttpStream& stream)=0;
};
struct HttpRequest {
    std::array<char,256> uri={{0}};
    std::int32_t content_length=-1;
    std::array<char,512> body={{0}};
    bool headers_complete=false;
    const char* error=nullptr;
    void fail(const char* message) noexcept {if(!error)error=message;}
};
struct WifiForm {
    std::array<char,48> ssid={{0}};
    std::array<char,64> password={{0}};
};
bool string_equal(const char* first,const char* second) noexcept; // 0x40203394
bool string_starts_with(const char* text,const char* prefix) noexcept; // 0x402033c8
char* find_character(char* text,char value) noexcept; // 0x4020da7c, does not find NUL
bool url_decode(char* destination,std::size_t capacity,const char* encoded,Ports& ports); // 0x402034e4
void parse_request_line(const char* line,HttpRequest& request,Ports& ports); // 0x40203410
// Complete returning parser, including yields, deadline refresh, header warnings
// and first-error preservation. An oversized URI/body is explicitly rejected
// instead of reproducing the OEM's unbounded/unterminated buffer writes.
void parse_http(HttpStream& stream,HttpRequest& request,HardwarePorts& hardware,Ports& ports,std::uint32_t timeout); // 0x40204b2c
void parse_wifi_form(HttpRequest& request,WifiForm& form,Ports& ports); // 0x402064ec
void parse_trigger(HttpRequest& request,std::array<char,10>& trigger,const char* expected,Ports& ports); // 0x40206924
void run_cstring_self_tests(Ports& ports); // 0x402035d0
void run_http_parser_self_tests(Ports& ports); // 0x40204cf8
void run_http_self_tests(Ports& ports); // convenience wrapper; application retains original interleaving
}}
