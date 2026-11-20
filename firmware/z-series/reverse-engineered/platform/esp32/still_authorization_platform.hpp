#pragma once
#include "zseries/still_authorization.hpp"
#include "http_client_platform.hpp"
#include <WiFiClientSecure.h>
namespace zseries { namespace oem { namespace esp32 {
// The OEM Still backend owns a WiFiClientSecure independently from its normal
// StillClient's WiFiClient, while both share the same HttpClient engine.
class StillAuthorizationBinding {
public:
    StillAuthorizationBinding(HttpClient&,HttpBindings&,const ConsoleServices&) noexcept;
    WiFiClientSecure socket;
    HttpBindings& transport;
    StillAuthorization authorization;
};
}}} // namespace zseries::oem::esp32
