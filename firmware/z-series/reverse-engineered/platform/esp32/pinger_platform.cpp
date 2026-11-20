#include "pinger_platform.hpp"
#include <WiFi.h>
#include <ping/ping.h>
namespace zseries { namespace oem { namespace esp32 {
PingerBinding* PingerBinding::active_=nullptr;
PingerBinding::PingerBinding(const ConsoleServices& diagnostics) noexcept {
    auto& s=pinger.services;s.context=this;s.diagnostics=diagnostics;
    s.wifi_status=[](void*,std::int32_t& result){result=static_cast<std::int32_t>(WiFi.status());return true;};
    s.set_option=[](void*,std::uint32_t option,std::uint32_t value){
        // OEM ignores esp_ping_set_target's error value.
        esp_ping_set_target(static_cast<ping_target_id_t>(option),&value,sizeof value);return true;
    };
    s.start=[](void*,std::int32_t& error){error=ping_init();return true;};
    s.resolve=[](void*,const char* host,std::uint32_t& address,std::int32_t& result){
        IPAddress value;result=WiFi.hostByName(host,value);address=static_cast<std::uint32_t>(value);return true;
    };
    s.parse=[](void*,const char* text,std::uint32_t& address){
        IPAddress value;value.fromString(text);address=static_cast<std::uint32_t>(value);return true;
    };
    s.register_commands=[](void* context){auto& b=*static_cast<PingerBinding*>(context);
        return b.register_commands&&b.register_commands(b.registry_context);};
}
PingerBinding::~PingerBinding(){if(previous_)active_=previous_;}
void PingerBinding::initialize_callback(){
    active_=nullptr;
    esp_ping_set_target(PING_TARGET_RES_FN,reinterpret_cast<void*>(&receive),sizeof(void*));
}
PingerResult PingerBinding::setup(){previous_=active_;active_=this;return pinger.setup();}
void PingerBinding::receive(ping_target_id_t,esp_ping_found* data){
    if(!active_||!data)return;
    PingStatistics value;value.response_ms=data->resp_time;value.timeouts=data->timeout_count;
    value.sent=data->send_count;value.received=data->recv_count;value.errors=data->err_count;
    value.bytes=data->bytes;value.total_bytes=data->total_bytes;value.total_ms=data->total_time;
    value.minimum_ms=data->min_time;value.maximum_ms=data->max_time;
    active_->pinger.receive(value);
}
}}} // namespace zseries::oem::esp32
