#include "still_authorization_platform.hpp"
namespace zseries { namespace oem { namespace esp32 {
StillAuthorizationBinding::StillAuthorizationBinding(HttpClient& http,HttpBindings& transport,const ConsoleServices& diagnostics) noexcept
    : transport(transport),authorization(http) {
    authorization.services.context=this;
    authorization.services.diagnostics=diagnostics;
    authorization.services.select_wifi_tls=[](void* context){
        auto& owner=*static_cast<StillAuthorizationBinding*>(context);
        owner.transport.client=&owner.socket;return true;
    };
}
}}} // namespace zseries::oem::esp32
