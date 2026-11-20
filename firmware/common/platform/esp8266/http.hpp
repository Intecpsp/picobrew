#pragma once
#include "picobrew/http_server.h"
#include <cstddef>
#include <cstdint>

namespace picobrew { namespace standalone { namespace esp8266 {
// Single foreground HTTP connection over the pinned SDK's raw lwIP callbacks.
// No accept/read/write/close method waits for network activity. Construction
// does no I/O. The caller owns PicoHttpServer deadlines and API authentication.
class HttpListener {
public:
    HttpListener() noexcept;
    ~HttpListener();
    HttpListener(const HttpListener&)=delete;
    HttpListener& operator=(const HttpListener&)=delete;
    bool begin(std::uint16_t port=80);
    void end() noexcept;
    bool listening() const noexcept;
    static const PicoHttpIo io;
    int accept();
    int receive(int client,std::uint8_t*,std::size_t);
    int send(int client,const std::uint8_t*,std::size_t);
    void close(int client) noexcept;
private:
    struct Impl;
    Impl* impl_;
};
}}}
