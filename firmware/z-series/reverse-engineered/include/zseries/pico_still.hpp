// Recovered Z119 PicoStill controller. Original addresses: analysis/pico-still.md.
#pragma once
#include "zseries/core_flow.hpp"
#include "zseries/wifi.hpp"
#include "zseries/console.hpp"
#include <array>
#include <cstdint>

namespace zseries { namespace oem {
struct PicoStillState {
    PicoStillStateId id = PicoStillStateId::Idle;
    PicoStillStateId previous = PicoStillStateId::Unknown;
    std::uint32_t sample_date = 0, sample_delta_ms = 0, response_date = 0;
    std::array<char, 65> error{};
    std::array<char, 17> selected{};
    std::array<std::array<char, 17>, 8> available{};
    std::array<bool, 8> available_truncated{};
    std::uint32_t available_count = 0;
    std::array<char, 33> version{}, ip{};
    std::int32_t consecutive_failures = 0;
    bool polling_stopped = false;
};
struct PicoStillObserved {
    Command pending = Command::Empty;
    bool client_error = false, client_retryable = false, client_done = false;
    bool authorization_pending = false, authorized = false, cleaned = false;
    bool client_succeeded() const noexcept { return !client_error && client_done; }
};
struct PicoStillCommand {
    StillCommandId id = StillCommandId::None;
    std::int32_t selection = -1;
};
enum class PicoStillEffect : std::uint8_t {
    HttpTick, ClientTick, AuthorizationTick, AcquireRuntime, ReleaseRuntime,
    WifiTarget, WifiConnect, AuthorizationRequest, ClientHost, ClientCommand,
    ClientConfigure, ClientClear, ScanCount, ScanSsid, EmitEvent, RegisterCommands
};
struct PicoStillAction {
    PicoStillEffect effect;
    std::uint32_t argument = 0;
    const char* text = nullptr;
    const WifiCredentials* credentials = nullptr;
    // ScanSsid writes a complete NUL-terminated SDK SSID into this destination.
    std::array<char, 33>* ssid = nullptr;
    explicit PicoStillAction(PicoStillEffect value) noexcept : effect(value) {}
};
struct PicoStillServices {
    void* context = nullptr;
    // Reports adapter completion independently from the original service result.
    // Client/backend operations refresh `observed` through the owning binding.
    bool (*perform)(void*, const PicoStillAction&, std::uint32_t& result) = nullptr;
    ConsoleServices diagnostics;
};
enum class PicoStillResult { Ok, MissingService, ServiceFailure, InvalidSelection };
class PicoStillController {
public:
    explicit PicoStillController(EepromData& settings) noexcept : configuration(settings) {}
    EepromData& configuration;
    PicoStillState state;
    PicoStillObserved observed;
    PicoStillServices services;
    PendingEvent<PicoStillCommand> commands;
    PendingEvent<CoreWifiEvent> wifi_events;
    WifiCredentials home_credentials;
    std::array<char, 33> selected_access_point{};
    const char* access_point_prefix = "PicoStill_";
    const char* access_point_password = "12345678";
    std::uint32_t poll_delay_ms = 2000, next_poll_date = 0, assertion_line = 0;
    std::int32_t maximum_retries = 2, failure_threshold = 4, retry_count = 0;

    // Original 0x400fc080, 0x4011ae0c, 0x4011a818, 0x4011a378.
    PicoStillResult setup();
    PicoStillResult tick(std::uint32_t now);
    PicoStillResult advance(std::uint32_t now);
    PicoStillResult transition(PicoStillStateId);
    // Original 0x4011a66c, 0x4011a78c, 0x400f8300.
    PicoStillResult retry(Command, std::int32_t maximum);
    PicoStillResult wait_for(PicoStillStateId next, Command retry_command, std::int32_t maximum);
    PicoStillResult collect_scan();
private:
    PicoStillResult diagnose(std::uint32_t line, const char* expression);
    PicoStillResult log(ConsoleLevel, std::uint32_t line, const char* format, ...);
    PicoStillResult perform(const PicoStillAction&, std::uint32_t* result = nullptr);
    PicoStillResult effect(PicoStillEffect, std::uint32_t argument = 0);
    PicoStillResult connect(const WifiCredentials&);
    PicoStillResult connect_still();
    PicoStillResult configure_client();
    PicoStillResult client_command(Command);
    void read_home_credentials() noexcept;
};
}} // namespace zseries::oem
