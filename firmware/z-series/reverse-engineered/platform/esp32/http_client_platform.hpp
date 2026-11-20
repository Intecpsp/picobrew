// Concrete socket operations through the historical Arduino Client interface.
#pragma once
#include "zseries/http_client.hpp"
#include <Client.h>
namespace zseries { namespace oem { namespace esp32 {
struct HttpBindings {
    void* context=nullptr;
    Client* client=nullptr;
    // Required only for HttpClient::configure(..., None). The application owns
    // the original Ethernet/WiFi/WiFiSecure factory and returned socket lifetime.
    bool (*acquire)(void*,bool secure,Client*&,HttpClientClass&)=nullptr;
    // Required only for the original Ethernet class's TLS/errno preparation.
    bool (*prepare_ethernet)(void*,bool secure)=nullptr;
};
// A caller can bind an existing historical WiFiClient or WiFiClientSecure as
// `client` and pass its explicit class, without an acquisition callback.
HttpServices http_services(HttpBindings&) noexcept;
}}} // namespace zseries::oem::esp32
