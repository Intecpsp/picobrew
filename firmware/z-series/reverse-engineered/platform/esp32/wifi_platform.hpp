// Concrete historical Arduino radio calls; application sinks remain required.
#pragma once
#include "zseries/wifi.hpp"

namespace zseries { namespace oem { namespace esp32 {
struct WifiBindings {
    void* context = nullptr;
    WifiManager* manager = nullptr;
    // A full mailbox drops a later event according to the original EventQ
    // contract. A successful delivery attempt may therefore return true
    // without replacing the pending event.
    bool (*emit)(void*, WifiEventType, WifiError, std::int32_t count) = nullptr;
    bool (*register_commands)(void*, WifiManager&) = nullptr;
};
// Bindings and manager must outlive all uses of the returned services.
WifiServices wifi_services(WifiBindings&) noexcept;
}}} // namespace zseries::oem::esp32
