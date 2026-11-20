// Z119 Wifi.h state machine. Radio/DNS/console services remain explicit.
#pragma once
#include "zseries/enums.hpp"
#include <array>
#include <cstdint>

namespace zseries { namespace oem {
struct WifiCredentials {
    std::array<char, 33> ssid{};
    std::array<char, 65> password{};
    bool internet_expected = false;
    bool equals(const WifiCredentials&) const noexcept;
};
struct WifiBackoff {
    bool active = false;
    std::uint32_t attempts = 0, deadline = 0, duration = 0;
    std::uint32_t initial_duration = 60000, maximum_duration = 480000;
    bool expired(std::uint32_t now) const noexcept { return active && deadline <= now; }
    void start(std::uint32_t now) noexcept;
    void advance(std::uint32_t now) noexcept;
    void stop() noexcept { active = false; }
};
struct WifiState {
    WiFiStateId id = WiFiStateId::Booting;
    std::int32_t rssi = 0;
    std::uint32_t level = 0;
    WifiCredentials requested, connected;
    // IPv4 octets occupy the low byte first, as in the original IPAddress.
    std::uint32_t access_point_address = 0x0104a8c0;
    std::uint32_t local_address = 0, gateway_address = 0, test_host_address = 0;
    bool local_address_present = false;
};
enum class WifiEffect : std::uint8_t {
    Status, Mode, Disconnect, SetMode, Scan, ScanComplete, AccessPoint,
    ConfigureAccessPoint, BeginConnection, Hostname, LocalAddress,
    GatewayAddress, Rssi, ResolveHost, Persistent, AutoConnect, MacAddress,
    RegisterCommands, EmitEvent
};
struct WifiAction {
    WifiEffect effect;
    std::uint32_t argument = 0, second = 0, third = 0, fourth = 0;
    const char* text = nullptr;
    const char* password = nullptr;
    const WifiCredentials* credentials = nullptr;
    std::array<std::uint8_t, 6>* mac = nullptr;
    std::uint32_t* address = nullptr;
    explicit WifiAction(WifiEffect value) noexcept : effect(value) {}
};
struct WifiServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    // Completion is distinct from the SDK result. Zero SDK results retain
    // the original application's behavior; missing services are errors.
    bool (*perform)(void*, const WifiAction&, std::uint32_t& result) = nullptr;
};
enum class WifiResult { Ok, MissingService, ServiceFailure };
class WifiManager {
public:
    WifiState state;
    WifiBackoff backoff;
    WifiServices services;
    WiFiStateId target = WiFiStateId::Idle;
    std::uint32_t state_date = 0, assertion_line = 0;
    std::uint32_t powerup_delay_ms = 500, disconnect_delay_ms = 1000,
                  connecting_timeout_ms = 20000;
    bool test_host_resolved = false;
    const char* access_point_name = "ZSeries";
    const char* test_host_name = "www.picobrew.com";
    std::array<std::uint8_t, 6> mac_address{};

    // Original 0x400f6ae8, 0x400f220c, 0x400f27e0, 0x400f2738.
    WifiResult setup();
    WifiResult transition(WiFiStateId);
    WifiResult tick(std::uint32_t now);
    WifiResult fail(std::uint32_t now, WifiError);
    // Original target/connect operations 0x400d5bc0 and 0x400e6b84.
    void select_target(WiFiStateId value) noexcept;
    void connect(const WifiCredentials&) noexcept;

private:
    WifiResult perform(const WifiAction&, std::uint32_t* result = nullptr);
    WifiResult effect(WifiEffect, std::uint32_t argument = 0,
                      std::uint32_t* result = nullptr);
    WifiResult event(WifiEventType, WifiError error = WifiError::None,
                     std::int32_t count = 0);
    WifiResult update_rssi();
};
}} // namespace zseries::oem
