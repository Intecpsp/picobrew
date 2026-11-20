// Owned Core composition. Platform, front-panel and console bindings are explicit.
#pragma once
#include "zseries/core_commands.hpp"
#include "zseries/core_control_state.hpp"
#include "zseries/core_helpers.hpp"
#include "zseries/backend_diagnostics.hpp"
#include "zseries/core_startup.hpp"
#include "zseries/azure.hpp"
#include "zseries/buzzer.hpp"
#include "zseries/head_ui_commands.hpp"
#include "zseries/http_download.hpp"
#include "zseries/local_http_application.hpp"
#include "zseries/physical_inputs.hpp"
#include "zseries/runtime_events.hpp"
#include "zseries/serial_console.hpp"
#include "zseries/serial_logger.hpp"
#include "zseries/still_http.hpp"
#include "zseries/system_tick.hpp"

namespace zseries { namespace oem {
class CoreApplication;
struct CoreApplicationPorts {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    // Setup hooks own the real platform setup and per-component console
    // registration. CoreApplication still invokes its recovered component
    // setup bodies; these are not substitutes for them.
    bool (*setup)(void*, CoreSetupStage) = nullptr;
    bool (*register_heater_commands)(void*) = nullptr;
    bool (*register_adc_commands)(void*) = nullptr;
    bool (*head_tick)(void*, std::uint32_t) = nullptr;
    bool (*head_wifi)(void*, const CoreWifiEvent&) = nullptr;
    bool (*head_runtime)(void*, const CoreRuntimeEvent&) = nullptr;
    bool (*head_still)(void*, const CoreStillEvent&) = nullptr;
    bool (*display_power)(void*, bool) = nullptr;
    bool (*azure_window_closed)(void*, bool) = nullptr;
    bool (*remote_session)(void*, const AzureCommand&, std::uint32_t encoder_activity_date) = nullptr;
    bool (*check_network)(void*, std::uint32_t, bool&) = nullptr;
    bool (*ethernet_connect)(void*, std::uint32_t) = nullptr;
    std::uint32_t (*ethernet_address)(void*) = nullptr;
    bool (*restart)(void*) = nullptr;
    bool (*select_factory)(void*) = nullptr;
    bool (*dispatch_text)(void*, const char*) = nullptr;
    bool (*scan_count)(void*, std::int32_t&) = nullptr;
    bool (*scan_ssid)(void*, std::uint8_t, std::array<char,33>&) = nullptr;
    // Bound to the real authorization controller, with results copied into
    // PicoStillObserved by the implementation before it returns.
    bool (*still_authorization)(void*, const PicoStillAction&, PicoStillObserved&) = nullptr;
    I2cTransport sensor_bus{};
    AcquisitionClock sensor_clock{};
    HeaterSynchronization control;
    HeaterIo heater_gpio{};
    SafetyIo safety_gpio{};
    CoreStartupServices timers{};
    SystemTickServices system_tick;
    ConsoleServices diagnostics;
    BackendScopeTrace backend_scope;
    SerialReaderServices serial_reader;
    const z_msp_image* msp_image = nullptr;
};

enum class ApplicationResult { Ok, MissingBinding, SetupFailure, ServiceFailure };
struct CoreApplicationStatus {
    ApplicationResult result = ApplicationResult::Ok;
    CoreSetupStage failed_setup = CoreSetupStage::Wifi;
    CoreTickStage failed_tick = CoreTickStage::Msp;
    CoreEffect failed_effect = CoreEffect::SetNetwork;
    std::uint32_t detail = 0;
};

// No component is copied after its references/callbacks have been bound.
// The Head CoreEvent mailbox is shared directly with both recipe publishers.
class CoreApplication {
public:
    explicit CoreApplication(CoreEventMailbox& head_events) noexcept;
    CoreApplication(const CoreApplication&) = delete;
    CoreApplication& operator=(const CoreApplication&) = delete;
    CoreApplicationPorts ports;
    CoreApplicationStatus status;
    EepromStore eeprom;
    CoreFlow core;
    TemperatureAcquisition acquisition;
    HeaterController heater;
    HeaterSensors temperatures;
    SafetyController safety_controller;
    SafetyState safeties;
    AuxiliarySensors auxiliary;
    Lis3dhtr accelerometer;
    Bmp280 pressure;
    z_msp_state msp_state{};
    z_msp_client msp{};
    ProgramBuffer program;
    ProgramRepository programs;
    CoreCommands commands;
    ProgramRuntime runtime;
    RuntimeHeaterBinding runtime_heater;
    PendingEvent<RuntimeCommand> runtime_commands;
    WifiManager wifi;
    Buzzer buzzer;
    SystemAlarmState alarm;
    SystemLoopTiming timing;
    CoreTimerHandles timers;
    PhysicalButton power_button;
    PhysicalEncoder encoder;
    SerialReaderState serial_reader;
    TextCommandMailbox text_commands;
    CoreEvent event;
    CoreEventMailbox& head_events;
    LocalRecipePublication publication;
    LocalHttpServer local_server;
    LocalHttpApplication local_application;
    HttpClient backend_http, session_http, firmware_http, binary_http, still_http;
    BackendState backend_state;
    HttpBackend backend;
    ZBackend sessions;
    FirmwareUpdate firmware_download;
    BinaryDownload binary_download;
    RegistrationState registration;
    ProgramDownloadState program_download;
    SessionLogger session_logger;
    SerialLogger serial_logger;
    CoreCsvSources csv;
    PendingEvent<AzureCommand> azure_commands;
    AzureService azure;
    PicoStillController still;
    PicoStillSamples still_samples;
    StillClient still_client;
    StillHttpBinding still_binding;
    std::uint32_t network = 0;
    std::array<char,13> identifier{};
    std::array<std::uint8_t,6> wifi_mac{}, ethernet_mac{};
    bool system_error = false, still_owns_wifi = false;
    std::array<char,65> system_error_text{};

    // Call after installing concrete ports and SDK services on owned objects.
    // setup_core_controls retains the original startup and timer ordering.
    ApplicationResult setup();
    ApplicationResult tick(std::uint32_t now);
    bool post(const HeadUiCoreEnvelope&);
    bool post(const CoreCommand&);
    // Direct publication to the same Core one-slot mailbox, with an owned
    // Azure session payload. Console producers must not use a second queue.
    bool post_azure(const AzureCommand&);
    bool post_text(const char*);
    bool post_runtime(const RuntimeCommand&);
    bool receive_wifi(const WifiAction&);
    bool perform(const CoreAction&, std::uint32_t& result);
    bool tick_stage(CoreTickStage, std::uint32_t now);
    bool setup_stage(CoreSetupStage);
    void refresh_observed() noexcept;
    SessionSnapshot session_snapshot() const noexcept;
    SessionLogSnapshot session_log_snapshot() const noexcept;
    bool emit(CoreEventType, std::uint32_t first=0, std::uint32_t second=0);
    bool helper(const HelperAction&, std::uint32_t&);
    bool still_effect(const PicoStillAction&, std::uint32_t&);
    bool network_ready(bool&);
private:
    HeadUiCoreEnvelope queued_command_, active_command_;
    AzureCommand queued_azure_, active_azure_;
    bool queued_has_azure_ = false, active_has_azure_ = false;
    std::array<char,129> active_text_{};
    // HttpBackend keeps a borrowed search pointer until its next request tick.
    // Its backing storage must outlive both the transient command and callback.
    std::array<char,17> backend_search_{};
    std::uint32_t now_ = 0;
    bool fail(std::uint32_t);
    bool diagnostic(const char*, std::uint32_t, const char*);
    bool assertion(const char*, std::uint32_t, const char*);
    bool backend_request(const CoreCommand&);
    bool azure_load();
    bool start_http_download(HttpClient&, const char*, const HttpReceiver&);
    bool setup_timer_services(CoreStartupServices&);
};
}} // namespace zseries::oem
