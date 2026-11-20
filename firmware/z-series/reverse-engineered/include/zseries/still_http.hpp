// Concrete bridge between the recovered StillClient and outgoing HTTP engine.
#pragma once
#include "zseries/http_client.hpp"
#include "zseries/still_client.hpp"
namespace zseries { namespace oem {
struct StillHttpBinding {
    HttpClient& http;
    StillClient& still;
    HttpResult last_result=HttpResult::Ok;
    StillHttpBinding(HttpClient& http,StillClient& still) noexcept : http(http),still(still) {}
};
// Binding and both clients must outlive all operations and response callbacks.
StillHttpServices still_http_services(StillHttpBinding&) noexcept;
// Use this for PicoStillEffect::HttpTick so asynchronous transport errors and
// state reach StillClient before its following ClientTick stage.
HttpResult tick_still_http(StillHttpBinding&,std::uint32_t now);
}} // namespace zseries::oem
