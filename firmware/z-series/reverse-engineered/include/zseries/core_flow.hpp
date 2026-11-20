// Original Z119 Core state transitions, event priority and service ordering.
#pragma once
#include "zseries/eeprom.hpp"
#include "zseries/program_runtime.hpp"
#include "zseries/program_repository.hpp"
#include <array>
#include <cstdint>

namespace zseries { namespace oem {

// Original EventQ is a one-slot mailbox: a second put is dropped while the
// previous value is pending. No RTOS queue or overwrite policy is substituted.
template<class T> struct PendingEvent {
    T value{};
    bool pending = false;
    bool put(const T& next) noexcept { if (pending) return false; value = next; pending = true; return true; }
    bool take(T& result) noexcept { if (!pending) return false; result = value; pending = false; return true; }
};

struct CoreCredentials {
    std::array<char, 33> ssid{};
    std::array<char, 65> password{};
    bool byte_80 = false;
};
struct CoreWifiEvent { WifiEventType type = WifiEventType::None; std::uint32_t argument = 0, second_argument = 0; };
struct CoreRuntimeEvent {
    RuntimeEventType type = RuntimeEventType::Transition;
    std::uint32_t previous = 0, current = 0;
    // Original RuntimeEvent +0x10 is independent of transition states.
    RuntimeNotificationType notification = RuntimeNotificationType::None;
};
struct CoreStillEvent { StillEventId type = StillEventId::None; PicoStillStateId state = PicoStillStateId::Unknown; };
struct CoreButtonEvent { std::uint32_t id = 0, type = 0, argument = 0; };

struct CoreCommand {
    CoreCommandId id = CoreCommandId::None;
    std::uint32_t program_id = 0;
    float temperature = 0;
    std::uint32_t duration = 0, location = 0;
    CoreCredentials credentials;
    std::uint32_t still_id = 0xffffffffu;
    ManualStepParameters manual;
    ProgramParameters program_parameters;
    // The original Core passes recipe/backend payloads through. Their concrete
    // types belong to those reconstructed services, not a fabricated Core ABI.
    const void* detail = nullptr;
};

struct CoreFlowState {
    CoreStateId id = CoreStateId::Off;
    std::uint32_t state_date = 0;
    bool service_mode = false, rinse_pending = false, offline = false;
    SessionType session_type = SessionType::Unknown;
    SessionMode session_mode = SessionMode::Unknown;
    std::uint32_t program_id = 0;
    bool runtime_start_pending = false;
    bool persist_credentials = false, ethernet_address_present = false;
    bool download_requested = false, session_stop_requested = false;
    bool azure_enabled = false;
    CoreCredentials pending_credentials;
    std::uint32_t assertion_line = 0;
};

struct CoreFlowObserved {
    bool temperature_polling_enabled = false;
    bool service_button_held = false;
    bool system_error = false;
    MspStateId msp_id = MspStateId::Unknown;
    std::uint32_t msp_errors = 0;
    std::array<std::uint32_t, 4> temperature_dates{};
    std::array<char, 65> sensor_error{};
    RuntimeStateId runtime_id = RuntimeStateId::Off;
    std::uint32_t encoder_activity_date = 0;
    std::uint32_t http_backend_state = 0;
    bool backend_sync_done = false, backend_error = false, registered = false;
    bool update_available = false;
    bool update_byte_11b58 = false, update_byte_11b68 = false;
    std::uint32_t update_client_state = 0;
    bool session_creation_error = false;
    std::uint32_t session_logger_state = 0;
    bool use_still_wifi = false;
};

enum class CoreTickStage : std::uint8_t {
    Msp, Temperatures, Accelerometer, Pressure, PicoStill, System, Safeties,
    PowerButton, FirmwareUpdate, BinaryDownload, SerialCommand, Wifi,
    HttpServer, ZBackend, Encoder, Head, Runtime, HttpBackend, SerialLogger,
    Registration, ProgramDownload, SessionLogger, Azure
};

// These are actual call boundaries in Core. Each needs an implementation;
// success no-ops for unrecovered services are not supplied by this module.
enum class CoreEffect : std::uint8_t {
    SetNetwork, WifiTarget, WifiConnect, StartSync, HeaterTimers,
    PrepareFirmwareUpdate, StartFirmwareUpdate, RegistrationRequest,
    RuntimeStop, DisplayPower, BeginSession, RuntimePause, EmitCoreEvent,
    EthernetConnect, EthernetAddress, SaveConfiguration, AzureRecover,
    RuntimeStart, StartProgram, HeadWifiEvent, StillWifiEvent, HttpServerEnable,
    HeadRuntimeEvent, AzureEndSession, DispatchCoreCommand, DispatchTextCommand,
    HeadStillEvent, Restart, RequestBackendCommand, AzureDiscard,
    UiAzureWindowClosed, ResetConfiguration, SelectFactoryPartition,
    AzureSessionLoad, RuntimePostCommand, ClearMspErrors, StillCommand,
    StartT1Monitor, CancelT1Monitor, AzureBeginSession, ReadRawTemperatures
};
struct CoreAction {
    CoreEffect effect;
    std::uint32_t argument = 0, second_argument = 0, third_argument = 0;
    const CoreCredentials* credentials = nullptr;
    const CoreWifiEvent* wifi = nullptr;
    const CoreRuntimeEvent* runtime = nullptr;
    const CoreStillEvent* still = nullptr;
    const CoreCommand* command = nullptr;
    const RuntimeCommand* runtime_command = nullptr;
    const void* text_command = nullptr;
    explicit CoreAction(CoreEffect value) noexcept : effect(value) {}
};

struct CoreFlowServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    bool (*tick)(void*, CoreTickStage, std::uint32_t) = nullptr;
    // bool reports adapter completion; result is the original application
    // result (network test success or Ethernet IPv4 address), where relevant.
    bool (*perform)(void*, const CoreAction&, std::uint32_t& result) = nullptr;
};
enum class CoreFlowResult { Ok, MissingService, ServiceFailure };

class CoreFlow {
public:
    explicit CoreFlow(EepromData& settings) noexcept : configuration(settings) {}
    CoreFlowState state;
    CoreFlowObserved observed;
    CoreFlowServices services;
    EepromData& configuration;
    std::uint32_t ethernet_timeout_ms = 10000;
    std::uint32_t standby_delay_ms = 1800000;
    std::uint32_t boot_delay_ms = 1100;
    PendingEvent<CoreWifiEvent> wifi_events;
    PendingEvent<CoreRuntimeEvent> runtime_events;
    PendingEvent<CoreCommand> commands;
    PendingEvent<CoreButtonEvent> button_events;
    PendingEvent<const void*> text_commands;
    PendingEvent<CoreStillEvent> still_events;

    // 0x4011cbf4, 0x4011ce5c, and 0x40120120 respectively.
    CoreFlowResult transition(CoreStateId);
    CoreFlowResult wifi_event(const CoreWifiEvent&);
    CoreFlowResult tick(std::uint32_t now_ms);
    CoreFlowResult advance(std::uint32_t now_ms);
    // 0x400f4bf4: deferred Runtime start and original rinse-type set.
    CoreFlowResult start_pending_runtime();
    // 0x4011d1e4: explicit session-mode selection while Starting.
    CoreFlowResult select_session_mode(CoreCommandId);

private:
    friend class CoreCommands;
    CoreFlowResult perform(const CoreAction&, std::uint32_t* result = nullptr);
    CoreFlowResult effect(CoreEffect, std::uint32_t argument = 0,
                          std::uint32_t second = 0, std::uint32_t third = 0);
    CoreFlowResult event(CoreEventType, std::uint32_t argument = 0,
                         std::uint32_t second = 0);
    CoreFlowResult recover_azure();
    CoreFlowResult save_configuration_with_timers();
    bool elapsed(std::uint32_t now, std::uint32_t duration) noexcept;
    bool msp_has_error() const noexcept;
};
}} // namespace zseries::oem
