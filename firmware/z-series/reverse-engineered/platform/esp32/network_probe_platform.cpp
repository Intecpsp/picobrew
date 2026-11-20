#include "network_probe_platform.hpp"
#include <WiFi.h>
namespace zseries { namespace oem { namespace esp32 {
NetworkProbeServices NetworkProbeBinding::services(){
    NetworkProbeServices result;result.context=this;result.diagnostics=connection.factory.services.diagnostics;
    result.wifi_status=[](void*,std::int32_t& status){status=WiFi.status();return true;};
    result.ethernet_acquire=[](void* context,bool& available){
        auto& self=*static_cast<NetworkProbeBinding*>(context);std::uintptr_t handle=0;HttpClientClass kind=HttpClientClass::None;
        const auto status=self.connection.factory.acquire(1,false,handle,kind,available);
        self.connection.http.client=reinterpret_cast<Client*>(handle);
        available=available&&handle!=0;return status==NetworkResult::Ok;
    };
    result.ethernet_connect=[](void* context,const char* host,std::uint16_t port,std::int32_t& value){
        auto* client=static_cast<NetworkProbeBinding*>(context)->connection.http.client;
        if(!client)return false;
        value=client->connect(host,port);return true;
    };
    result.ethernet_stop=[](void* context){
        auto* client=static_cast<NetworkProbeBinding*>(context)->connection.http.client;
        if(!client)return false;
        client->stop();return true;
    };
    return result;
}
}}} // namespace zseries::oem::esp32
