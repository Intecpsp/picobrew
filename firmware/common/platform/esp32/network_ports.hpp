#pragma once
#include "picobrew/standalone_engine.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <lwip/ip_addr.h>

namespace picobrew { namespace standalone { namespace esp32 {
// BSD nonblocking sockets avoid WiFiClient's foreground write/connect waits.
class HttpSocket {
public:
    bool begin(std::uint16_t port=80);
    PicoHttpIo io() const noexcept;
    void end();
private:
    int listener_=-1;
};
class TelemetrySocket {
public:
    picobrew::standalone::TelemetryIo services() noexcept;
    bool begin(const char*,std::uint16_t);
    int poll_connect();
    int send(const std::uint8_t*,std::size_t);
    int receive(std::uint8_t*,std::size_t);
    void close();
private:
    int socket_=-1;
    bool connecting_=false;
    portMUX_TYPE lock_=portMUX_INITIALIZER_UNLOCKED;
    bool dns_pending_=false,dns_done_=false,dns_valid_=false,abandoned_=false;
    std::uint32_t address_=0;
    std::uint16_t port_=0;
    char host_[254]={};
    static void resolve_start(void*);
    static void resolved(const char*,const ip_addr_t*,void*);
};
}}}
