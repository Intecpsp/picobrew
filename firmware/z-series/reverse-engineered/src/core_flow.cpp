#include "zseries/core_flow.hpp"
#include <cstring>

namespace zseries { namespace oem {
#define Z_CORE_TRY(expression) do { const auto result = (expression); if (result != CoreFlowResult::Ok) return result; } while (false)
namespace {
template<std::size_t N> void copy_text(std::array<char, N>& output, const char* input) noexcept {
    output.fill(0);
    if (!input) return;
    std::size_t i = 0; while (i + 1 < N && input[i]) { output[i] = input[i]; ++i; }
}
bool rinse_after(SessionType type) noexcept {
    const auto value = static_cast<std::uint32_t>(type);
    return (value >= 6 && value <= 9) || (value >= 12 && value <= 14);
}
} // namespace

CoreFlowResult CoreFlow::perform(const CoreAction& action, std::uint32_t* output) {
    if (!services.perform) return CoreFlowResult::MissingService;
    std::uint32_t result = 0;
    if (!services.perform(services.context, action, result)) return CoreFlowResult::ServiceFailure;
    if (output) *output = result;
    return CoreFlowResult::Ok;
}
CoreFlowResult CoreFlow::effect(CoreEffect operation, std::uint32_t argument,
                               std::uint32_t second, std::uint32_t third) {
    CoreAction action(operation); action.argument = argument; action.second_argument = second; action.third_argument = third;
    return perform(action);
}
CoreFlowResult CoreFlow::event(CoreEventType type, std::uint32_t argument, std::uint32_t second) {
    return effect(CoreEffect::EmitCoreEvent, static_cast<std::uint32_t>(type), argument, second);
}
bool CoreFlow::msp_has_error() const noexcept { return observed.msp_id != MspStateId::AppRunning || observed.msp_errors != 0; }
bool CoreFlow::elapsed(std::uint32_t now, std::uint32_t duration) noexcept {
    if (now < state.state_date) state.assertion_line = 0x486;
    return duration <= now - state.state_date;
}
CoreFlowResult CoreFlow::save_configuration_with_timers() {
    Z_CORE_TRY(effect(CoreEffect::HeaterTimers, 0));
    Z_CORE_TRY(effect(CoreEffect::SaveConfiguration));
    return effect(CoreEffect::HeaterTimers, 1);
}
CoreFlowResult CoreFlow::recover_azure() {
    state.azure_enabled = configuration.azure_enabled != BoolOption::False;
    if (state.azure_enabled) return effect(CoreEffect::AzureRecover, 1);
    return CoreFlowResult::Ok;
}

CoreFlowResult CoreFlow::transition(CoreStateId next) {
    if (next == state.id) return CoreFlowResult::Ok;
    if (!services.millis || !services.perform) return CoreFlowResult::MissingService;
    const auto previous = state.id;
    Z_CORE_TRY(event(CoreEventType::StateTransition, static_cast<std::uint32_t>(next), static_cast<std::uint32_t>(previous)));
    state.id = next;
    state.state_date = services.millis(services.context);
    switch (next) {
        case CoreStateId::Booting: state.service_mode = observed.service_button_held; break;
        case CoreStateId::ConnectingWiFi:
        case CoreStateId::ReConnectingWiFi: {
            Z_CORE_TRY(effect(CoreEffect::SetNetwork, static_cast<std::uint32_t>(NetworkType::None)));
            state.ethernet_address_present = false;
            if (!state.pending_credentials.ssid[0]) state.assertion_line = 0x284;
            CoreAction action(CoreEffect::WifiConnect); action.credentials = &state.pending_credentials;
            return perform(action);
        }
        case CoreStateId::ConnectingEthernet:
            Z_CORE_TRY(effect(CoreEffect::WifiTarget, 1)); state.ethernet_address_present = false; break;
        case CoreStateId::NeedsNetwork: return effect(CoreEffect::SetNetwork, 0);
        case CoreStateId::Syncing: return effect(CoreEffect::StartSync);
        case CoreStateId::Updating:
            Z_CORE_TRY(effect(CoreEffect::HeaterTimers, 0));
            observed.update_byte_11b58 = observed.update_byte_11b68 = false;
            Z_CORE_TRY(effect(CoreEffect::PrepareFirmwareUpdate));
            return effect(CoreEffect::StartFirmwareUpdate);
        case CoreStateId::Registering: return effect(CoreEffect::RegistrationRequest, 1);
        case CoreStateId::DownloadingProgram:
            state.download_requested = true;
            if (observed.backend_sync_done) return effect(CoreEffect::StartSync);
            break;
        case CoreStateId::StandBy:
            Z_CORE_TRY(effect(CoreEffect::RuntimeStop));
            return effect(CoreEffect::DisplayPower, 0);
        case CoreStateId::Idle: return effect(CoreEffect::RuntimeStop);
        case CoreStateId::Starting:
            if (state.session_mode == SessionMode::Online && state.session_type != SessionType::PicoStill)
                return effect(CoreEffect::BeginSession);
            break;
        case CoreStateId::Error: return effect(CoreEffect::RuntimePause);
        default: break; // Off, SyncPending, Running, Stopping and unrecognized values only change state.
    }
    return CoreFlowResult::Ok;
}

CoreFlowResult CoreFlow::wifi_event(const CoreWifiEvent& wifi) {
    const auto original_state = state.id;
    Z_CORE_TRY(effect(CoreEffect::SetNetwork, static_cast<std::uint32_t>(wifi.type == WifiEventType::Connected ? NetworkType::WiFi : NetworkType::None)));
    CoreAction forward(CoreEffect::HeadWifiEvent); forward.wifi = &wifi;
    if (observed.use_still_wifi) { forward.effect = CoreEffect::StillWifiEvent; return perform(forward); }
    if (original_state == CoreStateId::Idle) {
        if (wifi.type == WifiEventType::Disconnecting) Z_CORE_TRY(transition(CoreStateId::NeedsNetwork));
    } else if (original_state == CoreStateId::NeedsNetwork) {
        if (wifi.type == WifiEventType::Connected) {
            Z_CORE_TRY(transition(CoreStateId::Idle)); Z_CORE_TRY(recover_azure());
        }
    } else if (original_state == CoreStateId::ConnectingWiFi || original_state == CoreStateId::ReConnectingWiFi) {
        if (wifi.type == WifiEventType::Error) Z_CORE_TRY(transition(CoreStateId::NeedsNetwork));
        else if (wifi.type == WifiEventType::Connected) {
            if (state.persist_credentials) {
                configuration.use_ethernet = 0;
                eeprom_set_ssid(configuration, state.pending_credentials.ssid.data());
                eeprom_set_password(configuration, state.pending_credentials.password.data());
                Z_CORE_TRY(save_configuration_with_timers()); state.persist_credentials = false;
            }
            Z_CORE_TRY(transition(state.service_mode || original_state == CoreStateId::ReConnectingWiFi ? CoreStateId::Idle : CoreStateId::SyncPending));
            Z_CORE_TRY(effect(CoreEffect::HttpServerEnable, 1));
        }
    } else if (original_state == CoreStateId::StandBy && wifi.type == WifiEventType::Disconnecting) {
        Z_CORE_TRY(effect(CoreEffect::WifiTarget, 1));
    }
    return perform(forward);
}

CoreFlowResult CoreFlow::start_pending_runtime() {
    if (!state.runtime_start_pending) return CoreFlowResult::Ok;
    state.runtime_start_pending = false;
    if (!state.program_id) { state.assertion_line = 0x34a; return CoreFlowResult::Ok; }
    Z_CORE_TRY(effect(CoreEffect::RuntimeStart, state.program_id));
    state.rinse_pending = state.rinse_pending || rinse_after(state.session_type);
    return CoreFlowResult::Ok;
}

CoreFlowResult CoreFlow::select_session_mode(CoreCommandId command) {
    const bool supported = command == CoreCommandId::StartSessionOffline || command == CoreCommandId::StartSessionOnline;
    if (state.id != CoreStateId::Starting) state.assertion_line = 0x387;
    if (state.session_mode != SessionMode::Unknown) state.assertion_line = 0x388;
    if (!supported) state.assertion_line = 0x38a;
    if (state.id != CoreStateId::Starting || state.session_mode != SessionMode::Unknown || !supported) return CoreFlowResult::Ok;
    const auto selected = command == CoreCommandId::StartSessionOnline ? SessionMode::Online : SessionMode::Offline;
    if (state.session_type == SessionType::PicoStill && configuration.still_session_mode == SessionMode::Unknown) {
        configuration.still_session_mode = selected; Z_CORE_TRY(save_configuration_with_timers());
    }
    state.session_mode = selected;
    if (selected == SessionMode::Online && state.session_type != SessionType::PicoStill) return effect(CoreEffect::BeginSession);
    return CoreFlowResult::Ok;
}

CoreFlowResult CoreFlow::advance(std::uint32_t now) {
    switch (state.id) {
        case CoreStateId::Off: return transition(CoreStateId::Booting);
        case CoreStateId::Booting:
            if (observed.system_error || msp_has_error()) break;
            if (!observed.sensor_error[0]) {
                if (!observed.temperature_dates[1]) copy_text(observed.sensor_error, "WORT temp");
                else if (!observed.temperature_dates[0]) copy_text(observed.sensor_error, "HEX temp");
                else if (!observed.temperature_dates[2]) copy_text(observed.sensor_error, "DRAIN temp");
                else if (!observed.temperature_dates[3]) copy_text(observed.sensor_error, "AMBIENT temp");
            }
            if (observed.sensor_error[0]) break;
            if (state.service_mode) return transition(CoreStateId::Idle);
            if (!elapsed(now, boot_delay_ms)) return CoreFlowResult::Ok;
            if (configuration.use_ethernet) return transition(CoreStateId::ConnectingEthernet);
            if (!configuration.wifi_ssid[0]) return transition(CoreStateId::NeedsNetwork);
            copy_text(state.pending_credentials.ssid, configuration.wifi_ssid.data());
            copy_text(state.pending_credentials.password, configuration.wifi_password.data());
            return transition(CoreStateId::ConnectingWiFi);
        case CoreStateId::ConnectingEthernet: {
            Z_CORE_TRY(effect(CoreEffect::EthernetConnect, ethernet_timeout_ms));
            CoreAction request(CoreEffect::EthernetAddress); std::uint32_t address = 0;
            Z_CORE_TRY(perform(request, &address)); state.ethernet_address_present = address != 0;
            if (address) {
                request.effect = CoreEffect::SetNetwork; request.argument = static_cast<std::uint32_t>(NetworkType::Ethernet);
                std::uint32_t connected = 0; Z_CORE_TRY(perform(request, &connected));
                if (connected) {
                    configuration.use_ethernet = 1;
                    Z_CORE_TRY(effect(CoreEffect::SaveConfiguration));
                    return transition(state.service_mode ? CoreStateId::Idle : CoreStateId::SyncPending);
                }
                Z_CORE_TRY(event(CoreEventType::CoreError, 2));
            } else Z_CORE_TRY(event(CoreEventType::CoreError, 1));
            return transition(CoreStateId::NeedsNetwork);
        }
        case CoreStateId::NeedsNetwork:
            return state.offline ? transition(CoreStateId::Idle) : CoreFlowResult::Ok;
        case CoreStateId::SyncPending: return transition(CoreStateId::Syncing);
        case CoreStateId::Syncing:
            if (observed.http_backend_state) return CoreFlowResult::Ok;
            if (!observed.backend_error && observed.backend_sync_done) {
                if (observed.update_available) return transition(CoreStateId::Updating);
                if (!observed.registered) return transition(CoreStateId::Registering);
                Z_CORE_TRY(transition(CoreStateId::Idle)); return recover_azure();
            }
            break;
        case CoreStateId::Updating:
            if (!observed.update_byte_11b68 && !observed.update_client_state) return CoreFlowResult::Ok;
            break;
        case CoreStateId::Registering:
            if (!observed.backend_error) return observed.registered ? transition(CoreStateId::Idle) : CoreFlowResult::Ok;
            break;
        case CoreStateId::DownloadingProgram: break;
        case CoreStateId::Idle:
            if (observed.system_error || msp_has_error()) break;
            if (observed.runtime_id == RuntimeStateId::Standby && elapsed(now, standby_delay_ms)) {
                if (!services.millis) return CoreFlowResult::MissingService;
                if (standby_delay_ms < services.millis(services.context) - observed.encoder_activity_date)
                    return transition(CoreStateId::StandBy);
            }
            if (!state.rinse_pending) return CoreFlowResult::Ok;
            state.rinse_pending = false; return effect(CoreEffect::StartProgram, 1);
        case CoreStateId::Starting:
            if (state.session_mode == SessionMode::Unknown) return CoreFlowResult::Ok;
            if (state.session_mode == SessionMode::Online) {
                if (observed.session_creation_error) {
                    observed.session_creation_error = false; return event(CoreEventType::SessionCreationError);
                }
                if (observed.session_logger_state != 2 && state.session_type != SessionType::PicoStill) return CoreFlowResult::Ok;
            } else if (state.session_mode != SessionMode::Offline) {
                state.assertion_line = 0x5e4; return CoreFlowResult::Ok;
            }
            return start_pending_runtime();
        case CoreStateId::Stopping:
            if (observed.session_logger_state != 0) return CoreFlowResult::Ok;
            if (state.rinse_pending) { state.rinse_pending = false; return effect(CoreEffect::StartProgram, 1); }
            return transition(CoreStateId::Idle);
        default: return CoreFlowResult::Ok;
    }
    return transition(CoreStateId::Error);
}

CoreFlowResult CoreFlow::tick(std::uint32_t now) {
    if (!services.tick || !services.perform || !services.millis) return CoreFlowResult::MissingService;
    for (unsigned i = 0; i <= static_cast<unsigned>(CoreTickStage::Azure); ++i) {
        const auto stage = static_cast<CoreTickStage>(i);
        if (stage == CoreTickStage::Temperatures && !observed.temperature_polling_enabled) continue;
        if (!services.tick(services.context, stage, now)) return CoreFlowResult::ServiceFailure;
    }
    CoreWifiEvent wifi;
    if (wifi_events.take(wifi)) return wifi_event(wifi);
    CoreRuntimeEvent runtime;
    if (runtime_events.take(runtime)) {
        if ((state.id == CoreStateId::Idle || state.id == CoreStateId::Starting) &&
                runtime.type == RuntimeEventType::Transition && runtime.current == 3)
            Z_CORE_TRY(transition(CoreStateId::Running));
        else if (state.id == CoreStateId::Running && runtime.type == RuntimeEventType::Transition && runtime.current == 2) {
            state.session_stop_requested = true; Z_CORE_TRY(transition(CoreStateId::Stopping));
            Z_CORE_TRY(effect(CoreEffect::AzureEndSession));
        }
        CoreAction action(CoreEffect::HeadRuntimeEvent); action.runtime = &runtime; return perform(action);
    }
    CoreCommand command;
    if (commands.take(command)) { CoreAction action(CoreEffect::DispatchCoreCommand); action.command = &command; return perform(action); }
    CoreButtonEvent button;
    if (button_events.take(button)) {
        if (button.id != 0) state.assertion_line = 0x3d8;
        CoreCommand power; power.id = CoreCommandId::PowerButton; (void)commands.put(power);
        return CoreFlowResult::Ok;
    }
    const void* text = nullptr;
    if (text_commands.take(text)) { CoreAction action(CoreEffect::DispatchTextCommand); action.text_command = text; return perform(action); }
    CoreStillEvent still;
    if (still_events.take(still)) {
        if (still.type == StillEventId::StateTransition) {
            CoreAction action(CoreEffect::HeadStillEvent); action.still = &still; Z_CORE_TRY(perform(action));
            if (still.state == PicoStillStateId::Starting && state.session_mode == SessionMode::Online)
                return effect(CoreEffect::BeginSession);
        }
        return CoreFlowResult::Ok;
    }
    return advance(now);
}
#undef Z_CORE_TRY
}} // namespace zseries::oem
