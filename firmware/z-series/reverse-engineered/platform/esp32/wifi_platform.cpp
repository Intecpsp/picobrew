#include "wifi_platform.hpp"
#include <Arduino.h>
#include <WiFi.h>

namespace zseries { namespace oem { namespace esp32 {
namespace {
std::uint32_t clock_ms(void*) { return millis(); }
bool perform(void* context, const WifiAction& action, std::uint32_t& result) {
    WifiBindings& bindings = *static_cast<WifiBindings*>(context);
    result = 0;
    switch (action.effect) {
    case WifiEffect::Status: result = WiFi.status(); break;
    case WifiEffect::Mode: result = WiFi.getMode(); break;
    case WifiEffect::Disconnect: result = WiFi.disconnect(action.argument != 0); break;
    case WifiEffect::SetMode: result = WiFi.mode(static_cast<wifi_mode_t>(action.argument)); break;
    case WifiEffect::Scan:
        result = WiFi.scanNetworks(action.argument != 0, action.second != 0,
                                   action.third != 0, action.fourth); break;
    case WifiEffect::ScanComplete: result = WiFi.scanComplete(); break;
    case WifiEffect::AccessPoint:
        result = WiFi.softAP(action.text, action.password, action.argument,
                            action.second, action.third); break;
    case WifiEffect::ConfigureAccessPoint:
        result = WiFi.softAPConfig(IPAddress(action.argument), IPAddress(action.second),
                                  IPAddress(action.third)); break;
    case WifiEffect::BeginConnection:
        if (!action.credentials || action.second != 0) return false;
        result = WiFi.begin(action.credentials->ssid.data(), action.credentials->password.data(),
                            action.argument, nullptr, action.third != 0); break;
    case WifiEffect::Hostname: result = WiFi.setHostname(action.text); break;
    case WifiEffect::LocalAddress: result = static_cast<std::uint32_t>(WiFi.localIP()); break;
    case WifiEffect::GatewayAddress: result = static_cast<std::uint32_t>(WiFi.gatewayIP()); break;
    case WifiEffect::Rssi: result = static_cast<std::uint32_t>(WiFi.RSSI()); break;
    case WifiEffect::ResolveHost: {
        if (!action.address) return false;
        IPAddress address;
        result = WiFi.hostByName(action.text, address);
        *action.address = static_cast<std::uint32_t>(address); break;
    }
    case WifiEffect::Persistent: WiFi.persistent(action.argument != 0); break;
    // This deprecated historical SDK operation returns false. The original
    // application ignores that value; it is not setAutoReconnect().
    case WifiEffect::AutoConnect: result = WiFi.setAutoConnect(action.argument != 0); break;
    case WifiEffect::MacAddress:
        if (!action.mac) return false;
        WiFi.macAddress(action.mac->data()); break;
    case WifiEffect::RegisterCommands:
        return bindings.manager && bindings.register_commands &&
               bindings.register_commands(bindings.context, *bindings.manager);
    case WifiEffect::EmitEvent:
        return bindings.emit && bindings.emit(bindings.context,
               static_cast<WifiEventType>(action.argument), static_cast<WifiError>(action.second),
               static_cast<std::int32_t>(action.third));
    default: return false;
    }
    return true;
}
}
WifiServices wifi_services(WifiBindings& bindings) noexcept {
    WifiServices result; result.context = &bindings; result.millis = clock_ms; result.perform = perform;
    return result;
}
}}} // namespace zseries::oem::esp32
