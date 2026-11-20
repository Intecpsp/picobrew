#pragma once
#include "picostill/core.hpp"
#include <cstddef>

namespace picostill { namespace oem {
struct HttpStream {
    virtual ~HttpStream() = default;
    virtual bool connected() = 0;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual std::size_t write(const char* bytes,std::size_t size) = 0;
    virtual void flush() = 0;
    virtual void stop() = 0;
    virtual void decimal(double value,unsigned digits) = 0;
    virtual void unsigned_number(std::uint32_t value) = 0;
};
struct HttpRuntime {
    virtual ~HttpRuntime() = default;
    virtual std::uint32_t millis() = 0;
    virtual void cooperate() = 0;
    virtual void variable(const char* scope,const char* name,const char* value) = 0;
    virtual void integer(const char* scope,const char* name,std::uint32_t value) = 0;
    virtual void message(const char* value) = 0;
};
struct HttpListener {
    virtual ~HttpListener() = default;
    virtual void listen(unsigned port) = 0;
    virtual HttpStream* accept() = 0;
    virtual void release(HttpStream& stream) = 0;
};
struct HttpRequest {
    std::array<char,256> uri = {{0}};
    std::int32_t content_length = -1;
    std::array<char,512> body = {{0}};
    const char* error = nullptr;
    void fail(const char* text) noexcept {if(!error)error=text;}
};
bool string_equal(const char* first,const char* second) noexcept;
bool string_starts_with(const char* text,const char* prefix) noexcept;
char* find_character(char* text,char value) noexcept;
bool string_boolean(const char* text) noexcept;
bool url_decode(char* destination,std::size_t capacity,const char* encoded,Diagnostics& diagnostics);
void parse_request_line(const char* line,HttpRequest& request,HttpRuntime& runtime);
void parse_http(HttpStream& stream,HttpRequest& request,HttpRuntime& runtime,std::uint32_t timeout=5000);
struct WifiForm {
    std::array<char,48> ssid = {{0}};
    std::array<char,64> password = {{0}};
};
void parse_wifi_form(HttpRequest& request,WifiForm& form,Diagnostics& diagnostics);
}} // namespace picostill::oem
