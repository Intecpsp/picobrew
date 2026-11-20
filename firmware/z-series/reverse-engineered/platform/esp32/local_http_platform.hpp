// Real Arduino ESP32 1.0.1 incoming socket adapter.
#pragma once
#include "zseries/local_http_server.hpp"
#include <WiFiServer.h>
#include <WiFiClient.h>

namespace zseries { namespace oem { namespace esp32 {
class LocalHttpNetwork {
public:
    // Original Core constructor: port80, max_clients4; begin waits for enable.
    LocalHttpNetwork() : listener(80, 4) {}
    WiFiServer listener;
    WiFiClient client;
    // This object must outlive the server using its callbacks.
    void bind(LocalHttpServer&) noexcept;
};
}}} // namespace zseries::oem::esp32
