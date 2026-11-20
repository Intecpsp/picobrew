#include "picoferm/application.hpp"
#include <stdio.h>
namespace picoferm { namespace oem {
const char* state_name(State state) noexcept {static const char* const labels[]={"Undefined","Booting","Listening","Running","Logging","Connecting","Syncing","APMode","Calibrating","Validating","Finishing","Sleeping"};const auto index=static_cast<unsigned>(state);return index<12?labels[index]:"State::INVALID";}
Application::Application(Ports& ports,HardwarePorts& hardware,Runtime& runtime,Formatter& formatter,WifiPorts& network,
                         OutboundClient& client,SystemPorts& system,WebServices& web,const char* version,const char* website)
    : adc(hardware),acquisition(adc,ports,*this),leds(ports,hardware,*this),button(ports,hardware,*this),
      wifi(ports,hardware,network,*this),backend(ports,hardware,runtime,formatter,client,data,measurements.data),
      ports_(ports),hardware_(hardware),runtime_(runtime),formatter_(formatter),system_(system),web_(web){
    data.initialize(version,website,ports_);measurements.data.reset(ports_);wifi.ap_ssid=data.ap_ssid.data();wifi.website=data.website.data();
    leds.animation=Animation::Off;leds.steps=-1;leds.toggle=1;
}
void Application::load_storage(){
    if(system_.storage_size()>=PersistentData::size)system_.storage_read(measurements.data.bytes.data(),PersistentData::size);
    measurements.data.sanitize(ports_);
}
void Application::report_raw_storage(){
    PersistentData raw;raw.bytes.fill(0);const auto length=system_.storage_size();system_.storage_read(raw.bytes.data(),length<PersistentData::size?length:PersistentData::size);
    if(length<0x69)raw.word(PersistentData::pressure_offset_position,0);
    if(length<0x72)raw.word(PersistentData::slope_position,0);
    char name[33]={0},pass[65]={0};std::memcpy(name,raw.bytes.data(),32);std::memcpy(pass,raw.bytes.data()+32,64);
    ports_.integer("MEM","pslope",raw.word(PersistentData::slope_position));ports_.text("MEM","SSID",name);ports_.text("MEM","PASS",pass);
    ports_.boolean("MEM","hasWifi",raw.bytes[0x61]==1);ports_.integer("MEM","numLogs",raw.bytes[PersistentData::log_count_position]);
    ports_.boolean("MEM","hasPressure",raw.bytes[PersistentData::has_offset_position]!=0);ports_.integer("MEM","poffset",raw.word(PersistentData::pressure_offset_position));
}
void Application::setup(){
    std::uint8_t mac[6];system_.mac_address(mac);system_.storage_begin(storage_allocation);
    if(storage_allocation<PersistentData::size)diagnostic_assertion(ports_);
    report_raw_storage();load_storage();report_storage(measurements.data,ports_,formatter_);
    const auto stored_state=measurements.data.bytes[0x62];session_state=stored_state==2?SessionState::Idle:stored_state==10?SessionState::Fermenting:SessionState::Undefined;
    data.set_mac(mac,6,ports_);adc.begin(2,14,42);adc.configure_rate();adc.configure_reference();adc.configure_gain();wifi.initialize();backend.set_headers(data.website.data());
    button.initialize(measurements.data.bytes[0x63]);leds.initialize();web_.initialize_dns(300,2);data.update_token(ssid(),password(),ports_);transition(State::Booting);
}
void Application::begin(){
    system_.serial_begin(115200,0x1c,0,1);setup();system_.serial_line("0.2.7");system_.serial_line("0.2.7");
    system_.serial_line("8001aec764c5ce9336ede3cff887ec93c5c5265d");system_.serial_line("Oct 16 2018");system_.serial_line("16:48:11");
}
void Application::loop(){ports_.yield();poll();}
void Application::info(){
    // These are the original reference-image metadata, not this owned build's
    // compiler date or source revision. Live Data version/site follow below.
    ports_.text("MAIN","PICOFERM_VERSION","0.2.7");ports_.text("MAIN","GIT_BRANCH","0.2.7");ports_.text("MAIN","GIT_HASH","8001aec764c5ce9336ede3cff887ec93c5c5265d");
    ports_.text("MAIN","BUILD_DATE","Oct 16 2018");ports_.text("MAIN","BUILD_TIME","16:48:11");
    ports_.text("DATA","STATE",state_name(state));ports_.text("DATA","SESSION_STATE",session_name(session_state));ports_.text("DATA","VERSION",data.version.data());ports_.text("DATA","WEBSITE",data.website.data());
    ports_.text("DATA","UID",data.uid.data());ports_.text("DATA","APSSID",data.ap_ssid.data());ports_.text("DATA","TOKEN",data.token.data());
}
void Application::persist(){measurements.session_state=session_state;measurements.machine_state=static_cast<unsigned>(state);measurements.persist(ports_);}
void Application::restart(){ports_.text("MAIN","FUNC","ESP.restart");system_.restart();}
void Application::sleep(std::uint32_t seconds){
    ports_.integer("MAIN","SLEEP",seconds);ports_.text("MAIN","FUNC","ESP.deepSleep");ports_.text("MAIN","FUNC","ESP.removeOSWifiData");system_.remove_os_wifi_data();
    // The OEM multiply is 32-bit before its uint64_t SDK argument is formed.
    system_.deep_sleep(static_cast<std::uint32_t>(seconds*1000000u),0);
}
void Application::factory_reset(){
    ports_.text("MAIN","FUNC","FactoryReset");const auto offset=measurements.data.word(PersistentData::pressure_offset_position),slope=measurements.data.word(PersistentData::slope_position);const auto has_offset=measurements.data.bytes[PersistentData::has_offset_position];
    measurements.data.reset(ports_);measurements.data.word(PersistentData::pressure_offset_position,offset);measurements.data.word(PersistentData::slope_position,slope);measurements.data.bytes[PersistentData::has_offset_position]=has_offset;
    ports_.persist(measurements.data.bytes.data(),PersistentData::size);restart();
}
CalibrationStatus Application::calibration(){return measurements.data.calibration(ports_);}
void Application::calibrate(){ports_.text("MAIN","FUNC","CalibratePressure");calibrate_requested=true;}
void Application::configure(char* result,std::size_t capacity,const char* name,const char* pass){configure_credentials(data,measurements.data,name,pass,result,capacity,credentials_changed,ports_);}
void Application::measurement(float temperature,std::uint32_t raw_pressure){measurements.machine_state=static_cast<unsigned>(state);measurements.session_state=session_state;measurements.on_adc(temperature,raw_pressure,ports_);}
void Application::animation_changed(Animation){ports_.text("MAIN","CB","ledsStateChanged");}
void Application::long_press(){ports_.text("MAIN","CB","buttonPressed");button_requested=true;}
void Application::wifi_changed(WifiState previous,WifiState next){
    ports_.text("MAIN","CB","wifiStateChanged");char line[128];snprintf(line,sizeof(line),"%s -> %s",wifi_state_name(previous),wifi_state_name(next));ports_.text("MAIN","WIFI_TRANSITION",line);
    if(next==WifiState::AccessPoint){
        leds.set(Animation::AccessPointMode);if(!web_.start_dns(53,"*",0x0104a8c0))diagnostic_assertion(ports_);web_.start_http();server_enabled=true;web_.set_enabled(true);
    }else if(next==WifiState::Connected){backend.host_ip=wifi.host_ip;return;}
    else {if(!server_enabled)return;web_.stop_dns();server_enabled=false;web_.set_enabled(false);}
    ports_.boolean("SRV","ON",server_enabled);
}
bool Application::timed_out(std::uint32_t now,std::uint32_t duration){
    if(now<changed_at)diagnostic_assertion(ports_);
    if(duration==0)diagnostic_assertion(ports_);
    if(now-changed_at>duration){ports_.text("MAIN","TIMEOUT",state_name(state));return true;}return false;
}
void Application::transition(State next){
    if(next==state){diagnostic_warning(ports_);return;}
    button_requested=button_requested&&state!=State::APMode;char line[128];snprintf(line,sizeof(line),"%s -> %s",state_name(state),state_name(next));ports_.text("MAIN","STATE_TRANSITION",line);
    const bool led_finished=static_cast<std::uint32_t>(leds.steps)+1u<=1u;
    switch(next){
    case State::Booting:if(state!=State::Sleeping)diagnostic_assertion(ports_);leds.set(Animation::Busy);info();break;
    case State::Listening:allow_listen_timeout=true;run_requested=false;system_.serial_line(data.ap_ssid.data());system_.serial_line(data.uid.data());break;
    case State::Running:if(state!=State::Booting&&state!=State::Listening)diagnostic_assertion(ports_);break;
    case State::Logging:
        if(button.woke_board)diagnostic_assertion(ports_);
        if(wifi.state!=WifiState::Idle)diagnostic_assertion(ports_);
        if(!led_finished)diagnostic_assertion(ports_);
        leds.set(Animation::FermentationInProgress);acquisition.start();break;
    case State::Calibrating:acquisition.start();break;
    case State::Connecting:
        if(wifi.state!=WifiState::Idle)diagnostic_assertion(ports_);
        if(!acquisition.idle())diagnostic_assertion(ports_);
        if(!led_finished)diagnostic_assertion(ports_);
        wifi.connect(ssid(),password());leds.set(Animation::TalkingToPicobrew);break;
    case State::Validating:wifi.connect(ssid(),password());leds.set(Animation::TalkingToPicobrew);break;
    case State::Syncing:backend.sync(static_cast<std::int32_t>(sleep_seconds/60),battery_voltage(hardware_),!button.woke_board&&session_state==SessionState::Fermenting);break;
    case State::APMode:
        if(!acquisition.idle())diagnostic_assertion(ports_);
        if(!led_finished)diagnostic_assertion(ports_);
        credentials_changed=false;wifi.request(WifiState::AccessPoint);break;
    case State::Finishing:wifi.request(WifiState::Idle);break;
    case State::Sleeping:
        if(wifi.state!=WifiState::Idle)diagnostic_assertion(ports_);
        if(!acquisition.idle())diagnostic_assertion(ports_);
        if(!led_finished)diagnostic_assertion(ports_);
        persist();if(restart_requested)restart();else sleep(session_state==SessionState::Fermenting?sleep_seconds:0);break;
    default:diagnostic_warning(ports_);break;
    }
    state=next;changed_at=hardware_.millis();
}
bool Application::update(){
    leds.set(Animation::Updating);bool installed=false;
    if(!backend.status.needs_update||backend.status.update_address[0]==0)diagnostic_warning(ports_);
    else {
        char address[129]={0};std::memcpy(address,backend.status.update_address.data(),128);system_.update_reboot(false);ports_.text("MAIN","FUNC","ESPhttpUpdate.update");
        const int result=system_.update(address,"");
        if(result==2){ports_.text("OTA","UPDATE","HTTP_UPDATE_OK");installed=true;restart();}
        else if(result==0){ports_.text("OTA","UPDATE","HTTP_UPDATE_FAILED");const auto error=system_.update_error();ports_.text("OTA","ERROR",error.c_str());}
        else ports_.text("OTA","UPDATE",result==1?"HTTP_UPDATE_NO_UPDATES":"?");
    }
    leds.set(Animation::Busy);return installed;
}
void Application::handle_sync(){
    ports_.text("MAIN","FUNC","handleSync");
    if(backend.status.error)leds.set(Animation::GenericError);
    else if(backend.status.needs_update){if(!update())leds.set(Animation::GenericError);}
    else if(!backend.status.registered)leds.set(Animation::AccountNotAssociated);
    else if(backend.status.session.state==SessionState::Idle||backend.status.session.state==SessionState::Fermenting){
        session_state=backend.status.session.state;leds.set(session_state==SessionState::Idle?Animation::FermentationDone:
            backend.status.session.has_fermentation_error()?Animation::FermentationError:Animation::FermentationInProgress);
    }
    if(backend.status.data_sent){ports_.text("EPRM","API","clearDataPoints()");measurements.data.bytes[PersistentData::log_start_position]=0;measurements.data.bytes[PersistentData::log_count_position]=0;}
}
void Application::poll(){
    const auto start=hardware_.millis();acquisition.poll(hardware_);button.poll();leds.poll();wifi.poll();backend.poll();web_.poll();
    bool change=false;State next=state;
    switch(state){
    case State::Booting:next=battery_voltage(hardware_)>battery_threshold?State::Running:State::Listening;change=true;break;
    case State::Listening:
        if((allow_listen_timeout&&timed_out(start,listening_timeout))||run_requested||button_requested){next=State::Running;change=true;}else dispatch_console();break;
    case State::Running:
        if(button_requested||measurements.data.bytes[0x61]!=1)next=State::APMode;
        else if(button.woke_board)next=State::Connecting;else if(session_state==SessionState::Fermenting)next=State::Logging;else next=State::Finishing;
        change=true;break;
    case State::Logging:
        if(acquisition.complete()){next=measurements.data.bytes[PersistentData::log_count_position]>11?State::Connecting:State::Finishing;change=true;}
        else if(timed_out(start,acquisition_timeout)){next=State::Finishing;change=true;}
        break;
    case State::Connecting:
        if(button_requested){next=State::APMode;change=true;}
        else if(wifi.state==WifiState::Connected){next=State::Syncing;change=true;}
        else if(*wifi.error||timed_out(start,connection_timeout)){leds.set(Animation::GenericError);next=State::Finishing;change=true;}
        break;
    case State::Syncing:
        if(!backend.status.started)diagnostic_assertion(ports_);
        if(*wifi.error){leds.set(Animation::GenericError);next=State::Finishing;change=true;}
        else if(backend.status.done){handle_sync();next=State::Finishing;change=true;}
        break;
    case State::APMode:
        if(credentials_changed){next=State::Validating;change=true;}else if(calibrate_requested){next=State::Calibrating;change=true;}
        else if(timed_out(start,ap_timeout)){next=State::Finishing;change=true;}
        break;
    case State::Calibrating:if(acquisition.complete()||timed_out(start,acquisition_timeout)){next=State::Finishing;change=true;}
        break;
    case State::Validating:
        if(wifi.state==WifiState::Connected){ports_.text("MAIN","Validating","connected");measurements.data.bytes[0x61]=1;next=State::Syncing;change=true;}
        else if(*wifi.error||timed_out(start,validation_timeout)){
            bounded_copy(reinterpret_cast<char*>(measurements.data.bytes.data()),32,"",ports_);bounded_copy(reinterpret_cast<char*>(measurements.data.bytes.data()+32),64,"",ports_);measurements.data.bytes[0x61]=0;data.update_token(ssid(),password(),ports_);next=State::APMode;change=true;
        }break;
    case State::Finishing:
        if(button_requested){next=State::APMode;change=true;}
        else if((static_cast<std::uint32_t>(leds.steps)+1<=1&&wifi.state==WifiState::Idle)||timed_out(start,finishing_timeout)){next=State::Sleeping;change=true;}
        break;
    default:diagnostic_warning(ports_);break;
    }
    if(change)transition(next);
    const auto elapsed=hardware_.millis()-start;if(elapsed>5000)ports_.integer("MAIN","POLL_BAD",elapsed);
    if(poll_max<elapsed){poll_max=elapsed;ports_.integer("MAIN","POLL_MAX",elapsed);}
}
}}
