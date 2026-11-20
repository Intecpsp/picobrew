#include "zseries/pico_still.hpp"
#include "zseries/still_diagnostics.hpp"
#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
namespace {
template<std::size_t N> bool copy(std::array<char, N>& out, const char* value) noexcept {
    out.fill(0); if (!value) return false;
    const std::size_t length = std::strlen(value);
    std::memcpy(out.data(), value, std::min(length, N - 1)); return length >= N;
}
std::int32_t increment(std::int32_t value) noexcept {
    const std::uint32_t bits = static_cast<std::uint32_t>(value) + 1u;
    std::int32_t result; std::memcpy(&result, &bits, sizeof(result)); return result;
}
}
#define STILL_TRY(expr) do { const PicoStillResult r = (expr); if (r != PicoStillResult::Ok) return r; } while (false)
PicoStillResult PicoStillController::diagnose(std::uint32_t line, const char* expression) {
    ConsoleResult result;detail::StillDiagnosticSink sink(&services.diagnostics,result,assertion_line);
    if(sink.assertion("PicoStill.h",line,expression))return PicoStillResult::Ok;
    return result==ConsoleResult::MissingService?PicoStillResult::MissingService:PicoStillResult::ServiceFailure;
}
PicoStillResult PicoStillController::log(ConsoleLevel level,std::uint32_t line,const char* format,...) {
    if(!services.diagnostics.assertion&&!services.diagnostics.log)return PicoStillResult::Ok;
    if(!services.diagnostics.log)return PicoStillResult::MissingService;
    char message[512];va_list args;va_start(args,format);const int count=::vsnprintf(message,sizeof(message),format,args);va_end(args);
    if(count<0||static_cast<std::size_t>(count)>=sizeof(message))return PicoStillResult::ServiceFailure;
    return services.diagnostics.log(services.diagnostics.context,level,"STILLC",line?"PicoStill.h":"",line,message)?PicoStillResult::Ok:PicoStillResult::ServiceFailure;
}
PicoStillResult PicoStillController::perform(const PicoStillAction& action, std::uint32_t* result) {
    if (!services.perform) return PicoStillResult::MissingService;
    std::uint32_t value = 0;
    if (!services.perform(services.context, action, value)) return PicoStillResult::ServiceFailure;
    if (result) *result = value;
    return PicoStillResult::Ok;
}
PicoStillResult PicoStillController::effect(PicoStillEffect value, std::uint32_t argument) {
    PicoStillAction action(value); action.argument = argument; return perform(action);
}
PicoStillResult PicoStillController::connect(const WifiCredentials& credentials) {
    PicoStillAction action(PicoStillEffect::WifiConnect); action.credentials = &credentials; return perform(action);
}
PicoStillResult PicoStillController::connect_still() {
    WifiCredentials credentials; copy(credentials.ssid, selected_access_point.data());
    copy(credentials.password, access_point_password); return connect(credentials);
}
void PicoStillController::read_home_credentials() noexcept {
    home_credentials.ssid.fill(0); home_credentials.password.fill(0);
    std::memcpy(home_credentials.ssid.data(), configuration.wifi_ssid.data(), configuration.wifi_ssid.size());
    std::memcpy(home_credentials.password.data(), configuration.wifi_password.data(), configuration.wifi_password.size());
    home_credentials.internet_expected = true;
}
PicoStillResult PicoStillController::configure_client() {
    PicoStillAction action(PicoStillEffect::ClientConfigure); action.credentials = &home_credentials; return perform(action);
}
PicoStillResult PicoStillController::client_command(Command command) {
    return effect(PicoStillEffect::ClientCommand, static_cast<std::uint32_t>(command));
}
PicoStillResult PicoStillController::setup() {
    STILL_TRY(effect(PicoStillEffect::RegisterCommands));
    if (state.id != PicoStillStateId::Idle) STILL_TRY(diagnose(0x524,"_sState.Id == PicoStillStateId::Idle"));
    return PicoStillResult::Ok;
}
PicoStillResult PicoStillController::tick(std::uint32_t now) {
    STILL_TRY(effect(PicoStillEffect::HttpTick, now));
    STILL_TRY(effect(PicoStillEffect::ClientTick, now));
    STILL_TRY(effect(PicoStillEffect::AuthorizationTick, now));
    return advance(now);
}
PicoStillResult PicoStillController::transition(PicoStillStateId next) {
    if (state.id == next) return log(ConsoleLevel::Error,0x355,"Redundant call to setState %s",to_string(next));
    STILL_TRY(log(ConsoleLevel::Info,0,"TRANSITION = %s -> %s",to_string(state.id),to_string(next)));
    state.previous = state.id; state.id = next; retry_count = 0;
    PicoStillAction action(PicoStillEffect::ClientHost);
    if(next==PicoStillStateId::Unknown||static_cast<std::uint32_t>(next)==PicoStillStateIdCount)
        STILL_TRY(log(ConsoleLevel::Error,0x3a0,"Invalid newState %d",static_cast<int>(next)));
    switch (next) {
    case PicoStillStateId::Idle:
        state.available_count = 0; state.sample_delta_ms = 0; state.sample_date = 0;
        state.error.fill(0); state.version.fill(0); state.ip.fill(0); state.response_date = 0;
        state.polling_stopped = false; state.consecutive_failures = 0;
        // The original Idle and PendingStart cases share this release operation.
        STILL_TRY(effect(PicoStillEffect::ReleaseRuntime)); break;
    case PicoStillStateId::PendingStart: STILL_TRY(effect(PicoStillEffect::ReleaseRuntime)); break;
    case PicoStillStateId::Scanning: STILL_TRY(effect(PicoStillEffect::WifiTarget, 2)); break;
    case PicoStillStateId::SelectingStill: read_home_credentials(); break;
    case PicoStillStateId::ConnectingToHomeAP1: STILL_TRY(connect(home_credentials)); break;
    case PicoStillStateId::Authorizing:
    case PicoStillStateId::SendingCleanedAck:
        action.effect = PicoStillEffect::AuthorizationRequest;
        action.argument = next == PicoStillStateId::SendingCleanedAck;
        action.text = state.selected.data(); STILL_TRY(perform(action)); break;
    case PicoStillStateId::ConnectingToStillAP:
        STILL_TRY(connect_still()); action.text = "192.168.4.1"; STILL_TRY(perform(action)); break;
    case PicoStillStateId::PingingOnStillAP:
    case PicoStillStateId::PingingOnHomeAP: STILL_TRY(client_command(Command::version)); break;
    case PicoStillStateId::GettingIp: STILL_TRY(client_command(Command::stillip)); break;
    case PicoStillStateId::Configuring: read_home_credentials(); STILL_TRY(configure_client()); break;
    case PicoStillStateId::ConnectingStillToHomeAP:
        if (state.ip[0] == 0) STILL_TRY(diagnose(0x384,"_sState.Ip.length() > 0"));
        STILL_TRY(client_command(Command::connecttolan)); break;
    case PicoStillStateId::ConnectingToHomeAP2:
        if (state.ip[0] == 0) STILL_TRY(diagnose(0x388,"_sState.Ip.length() > 0"));
        STILL_TRY(connect(home_credentials)); action.text = state.ip.data(); STILL_TRY(perform(action)); break;
    case PicoStillStateId::Starting: STILL_TRY(client_command(Command::start)); break;
    case PicoStillStateId::Running: next_poll_date = 0; break;
    case PicoStillStateId::Stopping: STILL_TRY(client_command(Command::end)); break;
    default: break;
    }
    return effect(PicoStillEffect::EmitEvent, static_cast<std::uint32_t>(next));
}
PicoStillResult PicoStillController::retry(Command command, std::int32_t maximum) {
    if (retry_count < 0) STILL_TRY(diagnose(0x4a2,"_retryCount >= 0"));
    if (maximum < 0) STILL_TRY(diagnose(0x4a3,"maxRetry >= 0"));
    if (maximum <= retry_count) {
        STILL_TRY(log(ConsoleLevel::Error,0x4a5,"_retryCount (%d) >= maxRetry (%d) \"%s\"",retry_count,maximum,to_string(command)));
        return transition(command == Command::end ? PicoStillStateId::Idle : PicoStillStateId::Error);
    }
    STILL_TRY(log(ConsoleLevel::Warning,0x4a8,"_retryCount (%d) <  maxRetry (%d) \"%s\"",retry_count,maximum,to_string(command)));
    if (command == Command::configure) STILL_TRY(configure_client());
    else STILL_TRY(client_command(command));
    retry_count = increment(retry_count); return PicoStillResult::Ok;
}
PicoStillResult PicoStillController::wait_for(PicoStillStateId next, Command command, std::int32_t maximum) {
    if (observed.pending == Command::Empty) STILL_TRY(diagnose(0x4b5,"_sClient.hasPending()"));
    if (observed.client_succeeded()) {
        STILL_TRY(effect(PicoStillEffect::ClientClear)); return transition(next);
    }
    if (observed.client_error) {
        STILL_TRY(effect(PicoStillEffect::ClientClear)); return retry(command, maximum);
    }
    return PicoStillResult::Ok;
}
PicoStillResult PicoStillController::collect_scan() {
    std::uint32_t count = 0; PicoStillAction action(PicoStillEffect::ScanCount);
    STILL_TRY(perform(action, &count)); state.available_count = 0;
    const std::size_t prefix_length = std::strlen(access_point_prefix);
    // The SDK count is signed; -1/-2 represent scanning/failed.
    for (std::uint32_t i = 0; static_cast<std::int32_t>(i) < static_cast<std::int32_t>(count); ++i) {
        std::array<char, 33> ssid{}; action.effect = PicoStillEffect::ScanSsid;
        action.argument = static_cast<std::uint8_t>(i); action.ssid = &ssid; STILL_TRY(perform(action));
        if (std::strncmp(ssid.data(), access_point_prefix, prefix_length) != 0) continue;
        const auto index = state.available_count++;
        bool truncated=false;ConsoleResult result;detail::StillDiagnosticSink sink(&services.diagnostics,result,assertion_line);
        if(!sink.truncate(truncated,std::strlen(ssid.data()+prefix_length),state.available[index].size()-1))
            return result==ConsoleResult::MissingService?PicoStillResult::MissingService:PicoStillResult::ServiceFailure;
        state.available_truncated[index] = copy(state.available[index], ssid.data() + prefix_length);
        if (state.available_count == state.available.size()) {STILL_TRY(log(ConsoleLevel::Warning,0x33e,"Max list size reached"));break;}
    }
    return PicoStillResult::Ok;
}
PicoStillResult PicoStillController::advance(std::uint32_t now) {
    PicoStillCommand command; CoreWifiEvent wifi;
    if (wifi_events.take(wifi)) {
        if (wifi.type == WifiEventType::None) STILL_TRY(diagnose(0x3b7,"wifiEv.Type != WifiEventType::None"));
    } else if (commands.take(command)) {
        if (command.id == StillCommandId::None) STILL_TRY(diagnose(0x3ba,"cmd.Id != StillCommandId::None"));
        if (command.id == StillCommandId::Stop) {
            if (state.id == PicoStillStateId::Idle) return PicoStillResult::Ok;
            return transition(state.id == PicoStillStateId::Running ? PicoStillStateId::Stopping : PicoStillStateId::Idle);
        }
    }
    bool command_handled = false, wifi_handled = false;
    if(state.id==PicoStillStateId::Unknown||static_cast<std::uint32_t>(state.id)==PicoStillStateIdCount)
        STILL_TRY(log(ConsoleLevel::Error,0x48d,"Invalid state %d",static_cast<int>(state.id)));
    switch (state.id) {
    case PicoStillStateId::Idle:
        if (command.id == StillCommandId::Scan) {
            std::uint32_t allowed = 0; PicoStillAction action(PicoStillEffect::AcquireRuntime);
            STILL_TRY(perform(action, &allowed)); if (allowed) STILL_TRY(transition(PicoStillStateId::Scanning));
            else STILL_TRY(log(ConsoleLevel::Error,0x3cb,"Cannot acquire WiFi"));
            command_handled = true;
        } break;
    case PicoStillStateId::Scanning:
        if (wifi.type == WifiEventType::ScanStarted || wifi.type == WifiEventType::Disconnecting) wifi_handled = true;
        else if (wifi.type == WifiEventType::ScanCompleted) {
            STILL_TRY(collect_scan()); STILL_TRY(transition(PicoStillStateId::SelectingStill)); wifi_handled = true;
        } break;
    case PicoStillStateId::SelectingStill:
        if (command.id == StillCommandId::Scan) { STILL_TRY(transition(PicoStillStateId::Scanning)); command_handled = true; }
        else if (command.id == StillCommandId::Connect) {
            // Original pointer arithmetic is undefined outside the fixed 8-entry array.
            if (command.selection < 0 || static_cast<std::size_t>(command.selection) >= state.available.size())
                return PicoStillResult::InvalidSelection;
            state.selected = state.available[static_cast<std::size_t>(command.selection)];
            copy(selected_access_point, access_point_prefix);
            const std::size_t length = std::strlen(selected_access_point.data());
            for (std::size_t i = 0; i < state.selected.size() - 1 && length + i < selected_access_point.size() - 1 && state.selected[i]; ++i)
                selected_access_point[length + i] = state.selected[i];
            STILL_TRY(transition(PicoStillStateId::ConnectingToHomeAP1)); command_handled = true;
        } break;
    case PicoStillStateId::ConnectingToHomeAP1:
        if (wifi.type == WifiEventType::Connected) { STILL_TRY(transition(PicoStillStateId::Authorizing)); wifi_handled = true; }
        else if (wifi.type == WifiEventType::Error) { STILL_TRY(transition(PicoStillStateId::Error)); wifi_handled = true; }
        break;
    case PicoStillStateId::Authorizing:
    case PicoStillStateId::SendingCleanedAck:
        if (!observed.authorization_pending) STILL_TRY(transition(!observed.authorized ? PicoStillStateId::Error :
            !observed.cleaned ? PicoStillStateId::PendingCleanedAck : PicoStillStateId::ConnectingToStillAP));
        break;
    case PicoStillStateId::PendingCleanedAck:
        if (command.id == StillCommandId::CleanedAck) { STILL_TRY(transition(PicoStillStateId::SendingCleanedAck)); command_handled = true; }
        break;
    case PicoStillStateId::ConnectingToStillAP:
        if (wifi.type == WifiEventType::Disconnecting || wifi.type == WifiEventType::ScanStarted || wifi.type == WifiEventType::ScanCompleted) wifi_handled = true;
        else if (wifi.type == WifiEventType::Connected) { STILL_TRY(transition(PicoStillStateId::PingingOnStillAP)); wifi_handled = true; }
        else if (wifi.type == WifiEventType::Error) {
            if (retry_count < 3) { retry_count = increment(retry_count); STILL_TRY(connect_still()); }
            else STILL_TRY(transition(PicoStillStateId::Error));
            wifi_handled = true;
        } break;
    case PicoStillStateId::PingingOnStillAP: STILL_TRY(wait_for(PicoStillStateId::GettingIp, Command::version, 3)); break;
    case PicoStillStateId::GettingIp:
        if (observed.pending == Command::Empty) STILL_TRY(diagnose(0x425,"_sClient.hasPending()"));
        if (observed.client_error) {
            STILL_TRY(effect(PicoStillEffect::ClientClear)); STILL_TRY(retry(Command::stillip, maximum_retries));
        } else if (observed.client_succeeded()) {
            STILL_TRY(effect(PicoStillEffect::ClientClear));
            STILL_TRY(transition(std::strcmp(state.ip.data(), "0.0.0.0") == 0 ? PicoStillStateId::Configuring : PicoStillStateId::ConnectingStillToHomeAP));
        } break;
    case PicoStillStateId::Configuring: STILL_TRY(wait_for(PicoStillStateId::ConnectingToStillAP, Command::configure, maximum_retries)); break;
    case PicoStillStateId::ConnectingStillToHomeAP: STILL_TRY(wait_for(PicoStillStateId::ConnectingToHomeAP2, Command::Empty, 0)); break;
    case PicoStillStateId::ConnectingToHomeAP2:
        if (wifi.type == WifiEventType::Disconnecting || wifi.type == WifiEventType::ScanStarted || wifi.type == WifiEventType::ScanCompleted) wifi_handled = true;
        else if (wifi.type == WifiEventType::Connected) { STILL_TRY(transition(PicoStillStateId::PingingOnHomeAP)); wifi_handled = true; }
        else if (wifi.type == WifiEventType::Error) { STILL_TRY(transition(PicoStillStateId::Error)); wifi_handled = true; }
        break;
    case PicoStillStateId::PingingOnHomeAP: STILL_TRY(wait_for(PicoStillStateId::PendingStart, Command::version, maximum_retries)); break;
    case PicoStillStateId::PendingStart:
        if (command.id == StillCommandId::Start) { STILL_TRY(transition(PicoStillStateId::Starting)); command_handled = true; }
        else if (observed.client_error || observed.client_succeeded()) STILL_TRY(effect(PicoStillEffect::ClientClear));
        break;
    case PicoStillStateId::Starting: STILL_TRY(wait_for(PicoStillStateId::Running, Command::start, maximum_retries)); break;
    case PicoStillStateId::Running:
        if (observed.pending == Command::Empty) {
            if (command.id == StillCommandId::FanOn || command.id == StillCommandId::FanOff) {
                STILL_TRY(client_command(command.id == StillCommandId::FanOn ? Command::fanon : Command::fanoff)); command_handled = true;
            } else if (command.id == StillCommandId::Reconnect) {
                STILL_TRY(log(ConsoleLevel::Error,0x47e,"Trying still reconnect 1 time"));
                state.polling_stopped = false; state.consecutive_failures = 0; next_poll_date = now + poll_delay_ms; command_handled = true;
            } else if (!state.polling_stopped && next_poll_date < now) STILL_TRY(client_command(Command::data));
        } else {
            if (observed.client_succeeded()) state.consecutive_failures = 0;
            else if (observed.client_error) {
                if (observed.client_retryable) state.consecutive_failures = increment(state.consecutive_failures);
                STILL_TRY(log(ConsoleLevel::Error,0x472,"Problem communicating with PicoStill! TransientErrorCount = %d hasTransientError = %s",state.consecutive_failures,observed.client_retryable?"True":"False"));
                state.polling_stopped = !observed.client_retryable || failure_threshold < state.consecutive_failures;
            } else break;
            STILL_TRY(effect(PicoStillEffect::ClientClear)); next_poll_date = now + poll_delay_ms;
        } break;
    case PicoStillStateId::Stopping: STILL_TRY(wait_for(PicoStillStateId::Idle, Command::end, state.polling_stopped ? 0 : maximum_retries)); break;
    default: break;
    }
    if (!command_handled && command.id != StillCommandId::None) {
        STILL_TRY(diagnose(0x492,"cmd.Id == StillCommandId::None"));
        STILL_TRY(log(ConsoleLevel::Error,0x496,"%s ignored in state %s",to_string(command.id),to_string(state.id)));
    }
    if (!wifi_handled && wifi.type != WifiEventType::None) {
        STILL_TRY(diagnose(0x498,"wifiEv.Type == WifiEventType::None"));
        STILL_TRY(log(ConsoleLevel::Error,0x49c,"%s ignored in state %s",to_string(wifi.type),to_string(state.id)));
    }
    return PicoStillResult::Ok;
}
#undef STILL_TRY
}} // namespace zseries::oem
