#include "picostill/application.hpp"
#include <stdio.h>

namespace picostill { namespace oem {
void WifiStatus::connect(const char* ssid,const char* password,Diagnostics& diagnostics) {
    diagnostics.check(ssid && *ssid,"ssid != 0 && strlen(ssid) > 0");
    if(ssid && *ssid) {requested_ssid=ssid;requested_password=password;requested=WifiState::Connected;}
}
void Application::queue_button(bool is_long,std::uint32_t count) {
    hardware_.variable("MAIN","ButtonPressIsLong",is_long?"True":"False");services_.integer("MAIN","ButtonPressCount",count);
    diagnostics_.require(!button_pending,"PicoStill.h",0x9c,"!_buttonEventPending");
    button_long=is_long;button_count=count;button_pending=true;
}
void Application::queue_wifi(EventType type,StillError event_error) {
    hardware_.variable("MAIN","WifiEventType",to_string(type));
    diagnostics_.require(!wifi_pending,"PicoStill.h",0xa4,"!_wifiEventPending");
    wifi_error=event_error;wifi_event=type;wifi_pending=true;
    if(type==EventType::AccessPointReady && !http_listening) {services_.listen_http(80);http_listening=true;}
}
void Application::request_program(ProgramId program) noexcept {requested_program=program;program_pending=true;}
void Application::request_stop() {stop_pending=true;hardware_.variable("MAIN","STOP_PENDING","True");}
bool Application::select_menu(std::uint32_t index) {
    diagnostics_.require(menu_index<menu_count,"PicoStill.h",0x3f,"_index < TotalCount");
    // The original checks the old index, then reads an unchecked 3-item table.
    // Invalid new indices are an owned guard, outside successful OEM replay.
    if(index>=3) return false;
    menu_index=index;leds_.start(static_cast<Animation>(index+5));return true;
}
void Application::set_state(StillState next) {
    diagnostics_.require(static_cast<std::uint32_t>(next)<14,"PicoStill.h",0x132,"isEnumValid(newState)");
    if(state==next) {diagnostics_.message("Redundant call to setState");return;}
    char transition[128];snprintf(transition,sizeof(transition),"%s -> %s",to_string(state),to_string(next));
    hardware_.variable("MAIN","STATE_TRANSITION",transition);
    switch(next) {
    case StillState::WaitingSsid:wifi_.requested=WifiState::AccessPoint;break;
    case StillState::Configuring:
        leds_.start(Animation::Busy);diagnostics_.require(ssid[0]!='\0',"PicoStill.h",0x143,"strlen(_data.Ssid) > 0");credentials_changed=false;
        wifi_.connect(ssid.data(),password.data(),diagnostics_);break;
    case StillState::WaitingPico:connect_requested=false;wifi_.requested=WifiState::AccessPoint;break;
    case StillState::Connecting:leds_.start(Animation::Busy);wifi_.connect(ssid.data(),password.data(),diagnostics_);break;
    case StillState::Ready:program_pending=false;leds_.start(Animation::Ready);break;
    case StillState::CheckingUpdate:services_.request_firmware_address();break;
    case StillState::Updating:leds_.start(Animation::Updating);services_.start_firmware_update();break;
    case StillState::ManualProgramSelect:select_menu(0);break;
    case StillState::ProgramRunning:leds_.start(Animation::ProgramRunning);controller_.start(requested_program);stop_pending=false;break;
    case StillState::ProgramCompleted:leds_.start(Animation::SteadyGreen);break;
    case StillState::ErrorSending:
        diagnostics_.require(wifi_.state==WifiState::Connected,"PicoStill.h",0x16f,"_wifi.isConnected()");services_.send_error(error);break;
    case StillState::Error:leds_.start(error_animation(error,diagnostics_));break;
    default:break;
    }
    state=next;changed_at=hardware_.millis();
}
void Application::restart() {hardware_.variable("MAIN","COMMAND","restart");services_.restart();}
void Application::handle_button() {
    if(hardware_.millis()<reset_button_window && button_long) {services_.queue_console("reset");return;}
    switch(state) {
    case StillState::WaitingSsid:case StillState::WaitingPico:if(button_long)set_state(StillState::ManualProgramSelect);break;
    case StillState::Ready:if(button_long)set_state(StillState::WaitingPico);else if(button_count==2)restart();break;
    case StillState::ManualProgramSelect:
        if(button_count==1) {
            if(menu_count==0) {diagnostics_.check(false,"manual menu count is zero");return;} // Owned division guard.
            select_menu((menu_index+1u)%menu_count);
        } else if(button_count==2) {
            if(menu_index==0)request_program(ProgramId::Cleaning);
            else if(menu_index==1)request_program(ProgramId::PumpTest);
            else if(menu_index==2)request_program(ProgramId::FullTest);
        }
        break;
    case StillState::ProgramRunning:if(button_count==1){controller_.stop();requested_program=ProgramId::None;}break;
    case StillState::ProgramCompleted:case StillState::Error:restart();break;
    default:break;
    }
}
void Application::handle_wifi() {
    if(wifi_event==EventType::Error) {
        if(state==StillState::ProgramRunning)return;
        latch_first_error(error,wifi_error,diagnostics_);set_state(StillState::ErrorPending);
    } else if(wifi_event==EventType::AccessPointReady) {
        diagnostics_.require(state==StillState::WaitingSsid || state==StillState::WaitingPico,"PicoStill.h",0x1cd,
                             "_state == StillState::WaitingSsid || _state == StillState::WaitingPico");
        leds_.start(state==StillState::WaitingSsid?Animation::WaitingSsid:Animation::WaitingPico);
    } else if(wifi_event==EventType::Connected) {
        if(state==StillState::Configuring) {services_.save_credentials(stored);backend_.host_ip=wifi_.website_ip;set_state(StillState::CheckingUpdate);}
        else if(state==StillState::Connecting)set_state(StillState::Ready);
    }
}
void Application::update_state(std::uint32_t now) {
    const auto numeric=static_cast<std::uint32_t>(state);
    if(!(numeric-11u<3u) && error!=StillError::None) {set_state(StillState::ErrorPending);return;}
    switch(state) {
    case StillState::Booting:leds_.start(Animation::Booting);set_state(ssid[0]?StillState::Configuring:StillState::WaitingSsid);break;
    case StillState::WaitingSsid:if(ssid[0])set_state(StillState::Configuring);break;
    case StillState::CheckingUpdate:if(backend_.done)set_state(backend_.has_update_address?StillState::Updating:StillState::WaitingPico);break;
    case StillState::Updating:if(updater_state==0 || updater_state==6){services_.erase_nvs_after_update();restart();}break;
    case StillState::WaitingPico:
        if(connect_requested)set_state(StillState::Connecting);
        else if(credentials_changed)set_state(StillState::Configuring);
        break;
    case StillState::Ready:case StillState::ManualProgramSelect:
        if(program_pending){program_pending=false;set_state(StillState::ProgramRunning);}break;
    case StillState::ProgramRunning:
        if(stop_pending){stop_pending=false;controller_.stop();}
        if(controller_.running())return;
        if(controller_.error!=StillError::None){latch_first_error(error,controller_.error,diagnostics_);set_state(StillState::ErrorPending);}
        else set_state(controller_.program==ProgramId::Cleaning?StillState::ManualProgramSelect:StillState::ProgramCompleted);
        break;
    case StillState::ProgramCompleted:
        if(wifi_.state==WifiState::AccessPoint)set_state(StillState::WaitingSsid);
        else if(!(static_cast<std::uint32_t>(controller_.program)-1u<4u) && now>changed_at+completion_timeout)restart();
        break;
    case StillState::ErrorPending:set_state(wifi_.state==WifiState::Connected?StillState::ErrorSending:StillState::Error);break;
    case StillState::ErrorSending:if(wifi_.state!=WifiState::Connected || backend_.done)set_state(StillState::Error);break;
    default:break;
    }
}
void Application::poll() {
    const auto start=hardware_.millis();controller_.poll();services_.poll(ServicePoll::Update);services_.poll(ServicePoll::ConsoleReceiver);
    services_.poll(ServicePoll::Wifi);leds_.poll();button.poll();services_.poll(ServicePoll::Backend);services_.poll(ServicePoll::Http);
    services_.poll(ServicePoll::ConsoleCommands);
    if(button_pending){handle_button();button_pending=false;}
    else if(wifi_pending){handle_wifi();wifi_pending=false;}
    else update_state(start);
    const std::uint32_t duration=hardware_.millis()-start;
    if(duration>5000)services_.integer("MAIN","POLL_BAD",duration);
    if(duration>maximum_poll_time){maximum_poll_time=duration;services_.integer("MAIN","POLL_MAX",duration);}
}
}} // namespace picostill::oem
