#include "local_http_platform.hpp"

namespace zseries { namespace oem { namespace esp32 {
void LocalHttpNetwork::bind(LocalHttpServer& server) noexcept {
    auto& io = server.services.transport;
    io.context = this;
    io.connected = [](void* context) { return static_cast<LocalHttpNetwork*>(context)->client.connected() != 0; };
    io.accept = [](void* context) {
        auto& self = *static_cast<LocalHttpNetwork*>(context);
        self.client = self.listener.available(); return true;
    };
    io.available = [](void* context) { return static_cast<LocalHttpNetwork*>(context)->client.available(); };
    io.read = [](void* context) { return static_cast<LocalHttpNetwork*>(context)->client.read(); };
    io.close = [](void* context) { static_cast<LocalHttpNetwork*>(context)->client.stop(); return true; };
    io.stop_listener = [](void* context) { static_cast<LocalHttpNetwork*>(context)->listener.end(); return true; };
    io.begin_listener = [](void* context) {
        auto& listener = static_cast<LocalHttpNetwork*>(context)->listener;
        listener.begin(); return static_cast<bool>(listener);
    };
    io.write = [](void* context, const std::uint8_t* bytes, std::size_t size) {
        return static_cast<LocalHttpNetwork*>(context)->client.write(bytes, size) == size;
    };
}
}}} // namespace zseries::oem::esp32
