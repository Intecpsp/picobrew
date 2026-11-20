#include "zseries/core_commands.hpp"
#include <cstring>

namespace zseries { namespace oem {
#define Z_COMMAND_TRY(expression) do { const auto result = (expression); if (result != CoreFlowResult::Ok) return result; } while (false)

CoreFlowResult CoreCommands::delegate(CoreEffect effect, const CoreCommand& command) {
    CoreAction action(effect); action.command = &command; return core.perform(action);
}
CoreFlowResult CoreCommands::runtime_command(RuntimeCommandId id, const CoreCommand* source) {
    RuntimeCommand command; command.id = id;
    if (source) {
        if (id == RuntimeCommandId::SetTemperature) command.temperature = static_cast<double>(source->temperature);
        if (id == RuntimeCommandId::SetDuration) command.duration = source->duration;
        if (id == RuntimeCommandId::SetLocation) command.location = source->location;
        if (id == RuntimeCommandId::StartManualStep) command.manual = source->manual;
    }
    CoreAction action(CoreEffect::RuntimePostCommand); action.runtime_command = &command;
    return core.perform(action);
}
CoreFlowResult CoreCommands::restart() {
    Z_COMMAND_TRY(core.effect(CoreEffect::DisplayPower, 0));
    return core.effect(CoreEffect::Restart);
}
CoreFlowResult CoreCommands::reset(bool factory) {
    Z_COMMAND_TRY(core.effect(CoreEffect::HeaterTimers, 0));
    Z_COMMAND_TRY(core.effect(CoreEffect::ResetConfiguration));
    if (factory) Z_COMMAND_TRY(core.effect(CoreEffect::SelectFactoryPartition));
    return restart();
}
CoreFlowResult CoreCommands::connect_pending(const CoreCredentials& credentials) {
    core.state.pending_credentials = credentials;
    core.state.persist_credentials = true;
    return core.transition(CoreStateId::ConnectingWiFi);
}
CoreFlowResult CoreCommands::use_offline() {
    core.state.offline = true; core.state.azure_enabled = false;
    return core.effect(CoreEffect::WifiTarget, 1);
}

CoreFlowResult CoreCommands::start_program(std::uint32_t id) {
    if (core.state.id != CoreStateId::Idle && core.state.id != CoreStateId::Stopping) core.state.assertion_line = 0x2cf;
    const auto* program = programs.find(id);
    if (!program) return CoreFlowResult::Ok;
    session = CoreSession{};
    core.state.session_type = SessionType::Unknown;
    core.state.session_mode = SessionMode::Unknown;
    core.state.program_id = 0;
    if (!is_buffered_program(id)) {
        last_program_result = programs.static_stats(id, auxiliary.pressure_pa, temperatures.estimated_boil_celsius, programs.buffer().stats);
        if (last_program_result != ProgramBuildResult::Ok) return CoreFlowResult::ServiceFailure;
    }
    session.parameters = programs.buffer().parameters;
    session.stats = programs.buffer().stats;
    core.state.session_type = program->type;
    core.state.program_id = id;
    session.unique_program_id = program->unique_id;
    if (program->view.name) std::strncpy(session.name.data(), program->view.name, session.name.size() - 1);
    SessionMode selected = SessionMode::Online;
    if (core.state.offline) selected = SessionMode::Offline;
    else if (id < 27) {
        const auto bit = std::uint32_t(1) << id;
        if (bit & 0x000c4000u) selected = SessionMode::Offline;
        else if (bit & 0x04000080u) selected = core.configuration.still_session_mode;
    }
    core.state.session_mode = selected;
    session.use_azure = program_source_17b0 != 3 && core.configuration.azure_enabled != BoolOption::False;
    core.state.runtime_start_pending = true;
    Z_COMMAND_TRY(core.transition(CoreStateId::Starting));
    return core.effect(CoreEffect::AzureBeginSession);
}

CoreFlowResult CoreCommands::load_coffee(const CoreCommand& command) {
    if (command.id != CoreCommandId::CoffeeProgramLoad) core.state.assertion_line = 0x3a9;
    last_program_result = build_coffee_program(programs.buffer(), command.program_parameters, auxiliary.pressure_pa, core.configuration.use_imperial != 0);
    return last_program_result == ProgramBuildResult::Ok ? core.event(CoreEventType::ProgramLoaded) : CoreFlowResult::Ok;
}

CoreFlowResult CoreCommands::set_boiler(CoreCommandId command) {
    if (command == CoreCommandId::SetBoilerTypeSmall) core.configuration.boiler = BoilerType::Small;
    else if (command == CoreCommandId::SetBoilerTypeBig) core.configuration.boiler = BoilerType::Big;
    else core.state.assertion_line = 0x3c4;
    Z_COMMAND_TRY(core.save_configuration_with_timers());
    TempSensorType sensor;
    if (!hex_sensor_for_boiler(core.configuration.boiler, sensor)) return CoreFlowResult::ServiceFailure;
    acquisition.adc.sensor_types[0] = sensor;
    if (!heater_configure_boiler(heater, core.configuration.boiler)) return CoreFlowResult::ServiceFailure;
    return CoreFlowResult::Ok;
}

CoreFlowResult CoreCommands::dispatch(const CoreCommand& command) {
    const auto id = command.id;
    switch (core.state.id) {
        case CoreStateId::StandBy:
            if (id == CoreCommandId::PowerButton) return restart();
            if (id == CoreCommandId::AzureSessionLoad) {
                Z_COMMAND_TRY(core.transition(CoreStateId::Idle)); return delegate(CoreEffect::AzureSessionLoad, command);
            }
            return CoreFlowResult::Ok;
        case CoreStateId::Error:
            return id == CoreCommandId::PowerButton ? restart() : CoreFlowResult::Ok;
        case CoreStateId::Idle:
            switch (id) {
                case CoreCommandId::PowerButton: return core.event(CoreEventType::PowerButton);
                case CoreCommandId::StandBy: return core.transition(CoreStateId::StandBy);
                case CoreCommandId::DlAndRunProgram: return core.transition(CoreStateId::DownloadingProgram);
                case CoreCommandId::UseZOffline: return use_offline();
                case CoreCommandId::EthernetConnect: return core.transition(CoreStateId::ConnectingEthernet);
                case CoreCommandId::WiFiConnect: return connect_pending(command.credentials);
                case CoreCommandId::ResumeSession:
                case CoreCommandId::MyRecipeListGet: case CoreCommandId::ZKitRecipeListGet:
                case CoreCommandId::ZKitSmartListGet: case CoreCommandId::ZymaticRecipeLoad:
                case CoreCommandId::ZymaticRecipeRefLoad: case CoreCommandId::BrewPenRecipeLoad:
                case CoreCommandId::PicoPakRecipeLoad: case CoreCommandId::PicoPakRecipeRefLoad:
                case CoreCommandId::PicoPakShippedListGet:
                    return delegate(CoreEffect::RequestBackendCommand, command);
                case CoreCommandId::ResumeLoadedProgram: {
                    resumed_session = true;
                    CoreCommand start; start.id = CoreCommandId::StartProgram; start.program_id = 24;
                    (void)core.commands.put(start); return CoreFlowResult::Ok;
                }
                case CoreCommandId::AzureSessionLoad: return delegate(CoreEffect::AzureSessionLoad, command);
                case CoreCommandId::AzureWindowTimeout:
                    Z_COMMAND_TRY(core.effect(CoreEffect::UiAzureWindowClosed, 1));
                    return core.effect(CoreEffect::AzureDiscard);
                case CoreCommandId::AzureSessionDiscard: return core.effect(CoreEffect::AzureDiscard);
                case CoreCommandId::CoffeeProgramLoad: return load_coffee(command);
                case CoreCommandId::StartProgram: return start_program(command.program_id);
                case CoreCommandId::RuntimeStart: return core.start_pending_runtime();
                case CoreCommandId::EepromReset: return reset(false);
                case CoreCommandId::FactoryReset: return reset(true);
                case CoreCommandId::WiFiScan: return core.effect(CoreEffect::WifiTarget, 2);
                case CoreCommandId::BoilerDetect: {
                    Z_COMMAND_TRY(core.effect(CoreEffect::ReadRawTemperatures));
                    HexDetection result;
                    if (!detect_hex_from_adc(acquisition.adc.raw, result)) return CoreFlowResult::ServiceFailure;
                    const auto sensor = static_cast<std::uint32_t>(result.sensor);
                    const auto boiler = sensor == 3 ? BoilerType::Small : sensor == 0 ? BoilerType::Big : BoilerType::Unknown;
                    return core.event(CoreEventType::BoilerDetectResult, static_cast<std::uint32_t>(result.error), static_cast<std::uint32_t>(boiler));
                }
                case CoreCommandId::SetBoilerTypeBig: case CoreCommandId::SetBoilerTypeSmall: return set_boiler(id);
                case CoreCommandId::UseZOnline: return restart();
                default: return CoreFlowResult::Ok;
            }
        case CoreStateId::Starting:
            if (id == CoreCommandId::StartSessionOffline || id == CoreCommandId::StartSessionOnline) return core.select_session_mode(id);
            if (id == CoreCommandId::RuntimeStart) return core.start_pending_runtime();
            if (id == CoreCommandId::Stop) return core.transition(CoreStateId::Idle);
            return CoreFlowResult::Ok;
        case CoreStateId::Running:
            switch (id) {
                case CoreCommandId::PowerButton: case CoreCommandId::Pause: return core.effect(CoreEffect::RuntimePause);
                case CoreCommandId::Stop: return core.effect(CoreEffect::RuntimeStop);
                case CoreCommandId::Resume:
                    if (core.msp_has_error()) Z_COMMAND_TRY(core.effect(CoreEffect::ClearMspErrors, 0xfc));
                    return runtime_command(RuntimeCommandId::Resume);
                case CoreCommandId::Skip: return runtime_command(RuntimeCommandId::Skip);
                case CoreCommandId::Drain: return runtime_command(RuntimeCommandId::Drain);
                case CoreCommandId::ShowSetupInstructions: return runtime_command(RuntimeCommandId::ShowSetupInstructions);
                case CoreCommandId::RunPreBrewTests: return runtime_command(RuntimeCommandId::RunPreBrewTests);
                case CoreCommandId::SetTemperature: return runtime_command(RuntimeCommandId::SetTemperature, &command);
                case CoreCommandId::SetDuration: return runtime_command(RuntimeCommandId::SetDuration, &command);
                case CoreCommandId::SetLocation: return runtime_command(RuntimeCommandId::SetLocation, &command);
                case CoreCommandId::StartStep: return runtime_command(RuntimeCommandId::StartManualStep, &command);
                case CoreCommandId::PicoStillSelected: return core.effect(CoreEffect::StillCommand, 2, command.still_id);
                case CoreCommandId::PicoStillCleanedAck: return core.effect(CoreEffect::StillCommand, 5, 0xffffffffu);
                case CoreCommandId::PicoStillFanOn: return core.effect(CoreEffect::StillCommand, 6, 0xffffffffu);
                case CoreCommandId::PicoStillFanOff: return core.effect(CoreEffect::StillCommand, 7, 0xffffffffu);
                case CoreCommandId::PicoStillScan: return core.effect(CoreEffect::StillCommand, 1, 0xffffffffu);
                case CoreCommandId::PicoStillReconnect: return core.effect(CoreEffect::StillCommand, 8, 0xffffffffu);
                case CoreCommandId::T1MonitorActivate: return core.effect(CoreEffect::StartT1Monitor, 7, 1);
                case CoreCommandId::T1MonitorCancel: return core.effect(CoreEffect::CancelT1Monitor);
                case CoreCommandId::WiFiConnect: {
                    CoreAction action(CoreEffect::WifiConnect); action.credentials = &command.credentials; return core.perform(action);
                }
                default: return CoreFlowResult::Ok;
            }
        case CoreStateId::NeedsNetwork:
            if (id == CoreCommandId::PowerButton) return core.transition(CoreStateId::StandBy);
            if (id == CoreCommandId::UseZOffline) return use_offline();
            if (id == CoreCommandId::EthernetConnect) return core.transition(CoreStateId::ConnectingEthernet);
            if (id == CoreCommandId::WiFiScan) return core.effect(CoreEffect::WifiTarget, 2);
            if (id == CoreCommandId::WiFiConnect) return connect_pending(command.credentials);
            return CoreFlowResult::Ok;
        case CoreStateId::Registering:
            if (id == CoreCommandId::PowerButton) return core.transition(CoreStateId::StandBy);
            if (id == CoreCommandId::RefreshRegistrationCode) return core.effect(CoreEffect::RegistrationRequest, 0);
            return CoreFlowResult::Ok;
        default: return CoreFlowResult::Ok;
    }
}
#undef Z_COMMAND_TRY
}} // namespace zseries::oem
