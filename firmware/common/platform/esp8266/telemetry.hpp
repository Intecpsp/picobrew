#pragma once
#include "picobrew/standalone_engine.hpp"

namespace picobrew { namespace standalone { namespace esp8266 {

// One plain HTTP connection, using the SDK's asynchronous raw lwIP DNS/TCP API.
// Construction does not access the network. The engine owns deadlines/retries.
// No method waits for DNS, a TCP handshake, writable space or incoming bytes.
class AsyncTelemetry {
public:
    AsyncTelemetry() noexcept;
    ~AsyncTelemetry();
    AsyncTelemetry(const AsyncTelemetry&) = delete;
    AsyncTelemetry& operator=(const AsyncTelemetry&) = delete;
    TelemetryIo binding() noexcept;
    bool begin(const char* host, std::uint16_t port);
    int poll_connect();
    int send(const std::uint8_t* data, std::size_t length);
    int receive(std::uint8_t* data, std::size_t capacity);
    void close() noexcept;
private:
    struct Impl;
    Impl* impl_;
};

}}}
