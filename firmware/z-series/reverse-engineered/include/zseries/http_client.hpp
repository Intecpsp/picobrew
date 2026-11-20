// Recovered Z119 outgoing HTTP request and six-state asynchronous client.
#pragma once
#include "zseries/enums.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace zseries { namespace oem {
enum class HttpState : std::uint32_t { Idle, Initialized, WaitingReply, Headers, Body, Complete };
enum class HttpClientClass : std::uint32_t { None, Ethernet, Wifi, WifiSecure };
struct HttpRequest {
    std::uint16_t default_http_port=80,default_https_port=443,port=80;
    bool http=false,https=false,host_truncated=false,path_truncated=false,authorization_truncated=false;
    std::uint32_t address=0;
    std::array<char,65> host{};
    std::array<char,257> path{};
    std::array<char,513> authorization{};
    void reset() noexcept;
    // URI/authorization/type inputs are readable NUL-terminated C strings.
    // Fixed arrays are public for assembly but must retain that invariant.
    bool parse(const char*) noexcept;
    bool valid() const noexcept {return host[0]!=0&&!host_truncated&&!path_truncated;}
};
struct HttpResponse {
    std::int32_t status=0,content_length=-1;
    std::array<char,65> content_type{};
    bool content_type_truncated=false;
};
struct HttpReceiver {
    void* context=nullptr;
    bool (*headers)(void*,const HttpResponse&)=nullptr;
    void (*body)(void*,const std::uint8_t*,std::size_t)=nullptr;
    void (*complete)(void*)=nullptr;
};
enum class HttpEffect : std::uint8_t {
    AcquireClient, PrepareEthernet, FreeHeap, ConnectHost, ConnectAddress,
    Write, Flush, Available, ReadByte, ReadBytes, Connected, Stop, SocketError
};
struct HttpAction {
    HttpEffect effect;
    std::uint32_t argument=0,second=0;
    const char* text=nullptr;
    const std::uint8_t* data=nullptr;
    std::uint8_t* destination=nullptr;
    std::size_t size=0;
    HttpClientClass* acquired_class=nullptr;
    explicit HttpAction(HttpEffect value) noexcept : effect(value) {}
};
struct HttpServices {
    void* context=nullptr;
    std::uint32_t (*millis)(void*)=nullptr;
    // Completion and SDK return value are separate; socket/client/heap calls
    // cannot be replaced with implicit successful defaults.
    bool (*perform)(void*,const HttpAction&,std::uint32_t& result)=nullptr;
};
enum class HttpResult { Ok, MissingService, ServiceFailure, InvalidSpan };
struct HttpTiming {
    std::uint32_t send_date=0,headers_date=0,body_date=0;
    std::uint32_t send_duration=0,reply_duration=0,body_duration=0;
};
class HttpClient {
public:
    HttpRequest request;HttpResponse response;HttpReceiver receiver;HttpServices services;HttpTiming timing;
    HttpState state=HttpState::Idle;HttpClientClass client_class=HttpClientClass::None;
    bool client_present=false,stalled=false;
    HttpClientErrorId error=HttpClientErrorId::None;
    std::uint32_t socket_error=0,state_date=0,deadline=0,content_received=0,assertion_line=0;
    std::uint32_t reply_timeout_ms=5000,header_timeout_ms=5000;
    std::array<std::uint8_t,1025> buffer{};
    std::size_t buffer_length=0,buffer_remaining=1024;
    bool buffer_truncated=false,buffer_frozen=false;

    // Original 0x400f0124, 0x400db188, 0x400db2c8 and 0x400db324.
    void reset() noexcept;
    HttpResult transition(HttpState);
    HttpResult cancel();
    void set_error(HttpClientErrorId) noexcept;
    // Original 0x4010c7ec. A supplied client class binds an existing socket;
    // None requests the application's original network factory operation.
    HttpResult configure(const char* uri,const HttpReceiver&,HttpClientClass supplied,bool& accepted);
    bool set_authorization(const char*) noexcept;
    // Original GET/POST/PUT wrappers and request sender; payload lifetime need
    // only cover this synchronous write, response callbacks occur on later ticks.
    HttpResult get(bool& accepted);
    HttpResult post(const char* type,const void* body,std::size_t size,bool& accepted);
    HttpResult put(const char* type,const void* body,std::size_t size,bool& accepted);
    HttpResult tick(std::uint32_t now);
private:
    HttpResult perform(const HttpAction&,std::uint32_t* result=nullptr);
    HttpResult effect(HttpEffect,std::uint32_t* result=nullptr);
    void clear_buffer() noexcept;
    void append(const char*) noexcept;
    HttpResult flush_buffer(bool& written);
    HttpResult send(const char* method,const char* type,const void*,std::size_t,bool& accepted);
    HttpResult transmit(const char* method,const char* type,const void*,std::size_t,HttpClientErrorId& error);
    HttpResult read_headers(std::uint32_t now);
};
}} // namespace zseries::oem
