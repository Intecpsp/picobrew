#include "zseries/wifi.hpp"
#include <cstring>

namespace zseries { namespace oem {
bool WifiCredentials::equals(const WifiCredentials& other) const noexcept {
    return std::strncmp(ssid.data(), other.ssid.data(), ssid.size()) == 0 &&
           std::strncmp(password.data(), other.password.data(), password.size()) == 0 &&
           internet_expected == other.internet_expected;
}
void WifiBackoff::advance(std::uint32_t now) noexcept {
    ++attempts;
    deadline = now + duration;
    duration <<= 1;
    if (duration > maximum_duration) duration = maximum_duration;
}
void WifiBackoff::start(std::uint32_t now) noexcept {
    attempts = 0;
    duration = initial_duration;
    active = true;
    advance(now);
}
WifiResult WifiManager::perform(const WifiAction& action, std::uint32_t* output) {
    if (!services.perform) return WifiResult::MissingService;
    std::uint32_t value = 0;
    if (!services.perform(services.context, action, value)) return WifiResult::ServiceFailure;
    if (output) *output = value;
    return WifiResult::Ok;
}
WifiResult WifiManager::effect(WifiEffect kind, std::uint32_t argument, std::uint32_t* output) {
    WifiAction action(kind); action.argument = argument; return perform(action, output);
}
WifiResult WifiManager::event(WifiEventType type, WifiError error, std::int32_t count) {
    WifiAction action(WifiEffect::EmitEvent);
    action.argument = static_cast<std::uint32_t>(type);
    action.second = static_cast<std::uint32_t>(error);
    action.third = static_cast<std::uint32_t>(count);
    return perform(action);
}
#define WIFI_TRY(expression) do { const WifiResult checked = (expression); if (checked != WifiResult::Ok) return checked; } while (false)
WifiResult WifiManager::setup() {
    WIFI_TRY(effect(WifiEffect::Persistent, 0));
    WIFI_TRY(effect(WifiEffect::SetMode, 0));
    WIFI_TRY(effect(WifiEffect::AutoConnect, 0));
    WifiAction action(WifiEffect::MacAddress); action.mac = &mac_address;
    WIFI_TRY(perform(action));
    return effect(WifiEffect::RegisterCommands);
}
void WifiManager::select_target(WiFiStateId value) noexcept {
    target = value;
    backoff.stop();
}
void WifiManager::connect(const WifiCredentials& credentials) noexcept {
    if (credentials.ssid[0] == '\0') { assertion_line = 0x133; return; }
    state.requested = credentials;
    select_target(WiFiStateId::Connected);
}
WifiResult WifiManager::update_rssi() {
    std::uint32_t value = 0;
    WIFI_TRY(effect(WifiEffect::Rssi, 0, &value));
    const std::int32_t byte = static_cast<std::int32_t>(value & 255);
    state.rssi = byte >= 128 ? byte - 256 : byte;
    state.level = state.rssi >= 0 ? 0 : state.rssi >= -49 ? 4 :
                  state.rssi >= -59 ? 3 : state.rssi >= -69 ? 2 : 1;
    return WifiResult::Ok;
}
WifiResult WifiManager::transition(WiFiStateId next) {
    if (state.id == next) return WifiResult::Ok;
    if (!services.millis) return WifiResult::MissingService;
    state.id = next;
    state_date = services.millis(services.context);
    state.connected = WifiCredentials{};
    std::uint32_t value = 0;
    switch (next) {
    case WiFiStateId::Idle:
        WIFI_TRY(effect(WifiEffect::Mode, 0, &value));
        WIFI_TRY(effect(WifiEffect::Disconnect, 1));
        WIFI_TRY(effect(WifiEffect::SetMode, 0));
        state.level = 0;
        break;
    case WiFiStateId::Scanning: {
        WIFI_TRY(event(WifiEventType::ScanStarted));
        WifiAction action(WifiEffect::Scan); action.argument = 1; action.fourth = 300;
        WIFI_TRY(perform(action)); break;
    }
    case WiFiStateId::ScanCompleted:
        WIFI_TRY(effect(WifiEffect::ScanComplete, 0, &value));
        // Original truncates the nonnegative scan result to signed int16.
        value = static_cast<std::int32_t>(value) < 0 ? 0 : value & 65535;
        return event(WifiEventType::ScanCompleted, WifiError::None,
                     value >= 32768 ? static_cast<std::int32_t>(value) - 65536 : static_cast<std::int32_t>(value));
    case WiFiStateId::AccessPoint: {
        if (!access_point_name || !*access_point_name) assertion_line = 0xaa;
        WIFI_TRY(effect(WifiEffect::SetMode, 2));
        WifiAction config(WifiEffect::ConfigureAccessPoint);
        config.argument = state.access_point_address; config.second = state.access_point_address;
        config.third = 0x00ffffff;
        WIFI_TRY(perform(config));
        WifiAction action(WifiEffect::AccessPoint);
        action.text = access_point_name; action.password = "12345678";
        action.argument = 1; action.third = 4;
        WIFI_TRY(perform(action, &value));
        if (!value) assertion_line = 0xb2;
        return event(WifiEventType::AccessPointReady);
    }
    case WiFiStateId::PoweringUp:
        if (target == WiFiStateId::Scanning) WIFI_TRY(event(WifiEventType::ScanStarted));
        WIFI_TRY(effect(WifiEffect::SetMode, 1));
        break;
    case WiFiStateId::Connecting: {
        WifiAction action(WifiEffect::BeginConnection);
        action.credentials = &state.requested; action.third = 1;
        WIFI_TRY(perform(action));
        WifiAction hostname(WifiEffect::Hostname); hostname.text = access_point_name;
        WIFI_TRY(perform(hostname)); break;
    }
    case WiFiStateId::Checking: {
        WIFI_TRY(effect(WifiEffect::LocalAddress, 0, &state.local_address));
        state.local_address_present = true;
        WIFI_TRY(effect(WifiEffect::GatewayAddress, 0, &state.gateway_address));
        WIFI_TRY(update_rssi());
        if (!state.local_address) assertion_line = 199;
        test_host_resolved = false;
        WifiAction action(WifiEffect::ResolveHost); action.text = test_host_name;
        // SDK hostByName writes the address even when lookup returns failure.
        action.address = &state.test_host_address;
        WIFI_TRY(perform(action, &value));
        test_host_resolved = value == 1;
        break;
    }
    case WiFiStateId::Connected:
        backoff.stop(); state.connected = state.requested;
        return event(WifiEventType::Connected);
    case WiFiStateId::Disconnecting:
        state.level = 0;
        return event(WifiEventType::Disconnecting);
    default: break;
    }
    return WifiResult::Ok;
}
WifiResult WifiManager::fail(std::uint32_t now, WifiError error) {
    if (!backoff.active) backoff.start(now);
    else if (!backoff.expired(now)) assertion_line = 0x83;
    else backoff.advance(now);
    WIFI_TRY(transition(WiFiStateId::Idle));
    return event(WifiEventType::Error, error);
}
WifiResult WifiManager::tick(std::uint32_t now) {
    std::uint32_t status = 0, value = 0;
    WIFI_TRY(effect(WifiEffect::Status, 0, &status));
    switch (state.id) {
    case WiFiStateId::Booting: return transition(WiFiStateId::Idle);
    case WiFiStateId::Idle:
        if (target == WiFiStateId::Idle || (backoff.active && !backoff.expired(now))) break;
        return transition(WiFiStateId::PoweringUp);
    case WiFiStateId::Scanning:
        WIFI_TRY(effect(WifiEffect::ScanComplete, 0, &value));
        if (value == 0xffffffff) break;
        if (target == WiFiStateId::Scanning) select_target(WiFiStateId::ScanCompleted);
        return transition(WiFiStateId::ScanCompleted);
    case WiFiStateId::ScanCompleted:
        if (target == WiFiStateId::AccessPoint) return transition(WiFiStateId::AccessPoint);
        if (target == WiFiStateId::Connected) return transition(WiFiStateId::Connecting);
        if (target == WiFiStateId::ScanCompleted) break;
        if (target != WiFiStateId::Scanning && target != WiFiStateId::Idle) assertion_line = 0x16f;
        return transition(WiFiStateId::Idle);
    case WiFiStateId::AccessPoint:
        if (target != WiFiStateId::AccessPoint) return transition(WiFiStateId::AccessPointDisconnecting);
        break;
    case WiFiStateId::AccessPointDisconnecting:
    case WiFiStateId::Disconnecting:
        if (now > state_date + disconnect_delay_ms) return transition(WiFiStateId::Idle);
        break;
    case WiFiStateId::PoweringUp:
        if (now > state_date + powerup_delay_ms) return transition(WiFiStateId::Scanning);
        break;
    case WiFiStateId::Connecting:
        if (status == 3) return transition(WiFiStateId::Checking);
        if (status == 1) return fail(now, WifiError::WifiSsidNotFound);
        if (status == 0 || status == 6) {
            if (now > state_date + connecting_timeout_ms) return fail(now, WifiError::WifiConnectingTimeout);
            break;
        }
        return fail(now, WifiError::WifiConnectFailed);
    case WiFiStateId::Checking:
        if (!test_host_resolved) return fail(now, WifiError::WifiNoInternet);
        return transition(WiFiStateId::Connected);
    case WiFiStateId::Connected:
        if (status == 3 && target == WiFiStateId::Connected && state.requested.equals(state.connected)) return update_rssi();
        state.rssi = 0; state.level = 0;
        return transition(WiFiStateId::Disconnecting);
    default: break;
    }
    return WifiResult::Ok;
}
#undef WIFI_TRY
}} // namespace zseries::oem
