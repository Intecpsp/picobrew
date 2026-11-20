#include "application_platform.hpp"
#include "oem_credentials.hpp"
#include "zseries/http_client_console_tests.hpp"
#include "zseries/azure_console_tests.hpp"
#include "zseries/network_console_tests.hpp"
#include <EEPROM.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace zseries { namespace oem { namespace esp32 {
namespace {
FirmwareApplication* active_application=nullptr;
FirmwareApplication& owner(void* p){return *static_cast<FirmwareApplication*>(p);}
const char* boolean(bool value){return value?"True":"False";}
double seconds(std::uint32_t value){return static_cast<double>(static_cast<float>(value)/1000.0f);}
std::uint32_t clock_ms(void*){return ::millis();}
template<std::size_t N>void copy_text(std::array<char,N>& out,const char* text){
    out.fill(0);if(text)std::memcpy(out.data(),text,std::min(N-1,std::strlen(text)));
}
ConsoleServices prepare_console(FirmwareApplication& self){
    auto& b=self.console_bindings;b.input=&Serial;b.output=&Serial;
    b.commands=&self.core.text_commands;b.queue_name="Core::TextCommandEventQ";
    b.diagnostics=&self.diagnostic_state;b.diagnostic_services=serial_diagnostic_services();
    return console_services(b);
}
bool report_ip(FirmwareApplication& app,const char* tag,const char* name,std::uint32_t value){
    return app.log(ConsoleLevel::Info,tag,nullptr,0,"%s = %u.%u.%u.%u",name,
        value&255,(value>>8)&255,(value>>16)&255,(value>>24)&255);
}
bool report_mac(FirmwareApplication& app,const char* tag,const char* name,const std::array<std::uint8_t,6>& value){
    return app.log(ConsoleLevel::Info,tag,nullptr,0,"%s = %02x:%02x:%02x:%02x:%02x:%02x",name,
        value[0],value[1],value[2],value[3],value[4],value[5]);
}
}

FirmwareApplication::FirmwareApplication()
    :core(head.core_events),diagnostics(prepare_console(*this)),console(core,diagnostics),
     display_console(head.display,head.display_text_context,head.animation.dirty),
     backend_socket(core.network,diagnostics,&Serial),session_socket(core.network,diagnostics,&Serial),
     firmware_socket(core.network,diagnostics,&Serial),binary_socket(core.network,diagnostics,&Serial),
     test_socket(core.network,diagnostics,&Serial),
     probe(diagnostics,&Serial),azure(core.network,diagnostics,&Serial),
     authorization(core.still_http,still_transport,diagnostics),pinger(diagnostics),
     thermal_limits(original_thermal_limits()) {
    buzzer_mutex=xSemaphoreCreateRecursiveMutex();
    bind();
    construction_ok=buzzer_mutex&&
        initialize_msp(core.msp,core.msp_state,system)==Z_MSP_OK;
    // initialize_msp clears the client; the borrowed sink must be bound after it.
    msp_diagnostics.context=this;
    msp_diagnostics.assertion=[](void* p,const char* file,std::uint32_t line,const char* expression){return owner(p).assertion(file,line,expression);};
    msp_diagnostics.log=[](void* p,unsigned level,const char* tag,const char* file,std::uint32_t line,const char* message){return owner(p).log(static_cast<ConsoleLevel>(level),tag,file,line,"%s",message);};
    msp_diagnostics.trace=[](void* p,bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){return owner(p).scope(begin,tag,file,line,name);};
    msp_diagnostics.assertion_count=[](void* p){return owner(p).diagnostic_state.assertion_count;};
    msp_diagnostics.watchdog_reset=[](void*){esp_task_wdt_reset();return true;};
    core.msp.diagnostics=&msp_diagnostics;
}

void FirmwareApplication::bind(){
    device_identity(ESP.getEfuseMac(),core.identifier,core.wifi_mac,core.ethernet_mac);
    head.bind_settings(core.eeprom.data);
    auto& ports=core.ports;ports.context=this;ports.millis=clock_ms;ports.diagnostics=diagnostics;
    ports.setup=[](void* p,CoreSetupStage stage){return owner(p).setup_component(stage);};
    ports.register_heater_commands=[](void* p){return owner(p).console.register_heater()==ConsoleResult::Ok;};
    ports.register_adc_commands=[](void* p){return owner(p).console.register_max11613()==ConsoleResult::Ok;};
    ports.head_tick=[](void* p,std::uint32_t now){auto& a=owner(p);a.observe_head();return a.head.tick(now)==HeadApplicationResult::Ok;};
    ports.head_wifi=[](void* p,const CoreWifiEvent& value){owner(p).head.receive_wifi(value);return true;};
    ports.head_runtime=[](void* p,const CoreRuntimeEvent& value){owner(p).head.receive_runtime(value);return true;};
    ports.head_still=[](void* p,const CoreStillEvent& value){owner(p).head.receive_still(value);return true;};
    ports.display_power=[](void* p,bool enabled){return owner(p).screen_power(enabled);};
    ports.azure_window_closed=[](void* p,bool){HeadAzureEvent value;value.type=AzureEventType::TimeoutStartProgram;owner(p).head.receive_azure(value);return true;};
    ports.remote_session=[](void* p,const AzureCommand& value,std::uint32_t activity){auto& a=owner(p);a.observe_head();return a.head.handle_remote_session(value,activity)==HeadApplicationResult::Ok;};
    ports.check_network=[](void* p,std::uint32_t mode,bool& connected){return check_network_connectivity(mode,owner(p).probe.services(),connected)==NetworkResult::Ok;};
    ports.ethernet_connect=[](void* p,std::uint32_t timeout){return owner(p).ethernet_connect(timeout);};
    ports.ethernet_address=[](void*){return esp32::ethernet_address();};
    ports.restart=[](void* p){return owner(p).restart();};
    ports.select_factory=[](void* p){return owner(p).select_factory();};
    ports.dispatch_text=[](void* p,const char* value){return owner(p).console.dispatch(value)==ConsoleResult::Ok;};
    ports.scan_count=[](void*,std::int32_t& count){count=WiFi.scanComplete();return true;};
    ports.scan_ssid=[](void*,std::uint8_t index,std::array<char,33>& ssid){copy_text(ssid,WiFi.SSID(index).c_str());return true;};
    ports.still_authorization=[](void* p,const PicoStillAction& action,PicoStillObserved& observed){
        auto& a=owner(p);auto& auth=a.authorization.authorization;
        const auto result=action.effect==PicoStillEffect::AuthorizationRequest?
            auth.request(a.core.identifier.data(),action.text,action.argument!=0):auth.tick();
        auth.observe(observed);return result==StillAuthorizationResult::Ok;
    };
    ports.sensor_bus=i2c(system.sensor_bus);ports.sensor_clock=acquisition_clock();
    ports.heater_gpio=gpio();ports.safety_gpio=safety_input();ports.msp_image=&production_msp_image;
    ports.control.context=this;
    ports.control.enter=[](void* p){portENTER_CRITICAL(&owner(p).thermal_lock);};
    ports.control.leave=[](void* p){portEXIT_CRITICAL(&owner(p).thermal_lock);};
    timer_callbacks.buzzer=buzzer_timer;timer_callbacks.heater_second=heater_second;timer_callbacks.heater_fast=heater_fast;
    ports.timers={};ports.timers.context=&timer_callbacks;
    ports.timers.start_periodic_software_timer=start_periodic_software_timer;
    ports.timers.begin_hardware_timer=begin_hardware_timer;ports.timers.attach_timer_interrupt=attach_timer_interrupt;
    ports.timers.write_timer_alarm=write_timer_alarm;ports.timers.enable_timer_alarm=enable_timer_alarm;
    ports.serial_reader=serial_reader_services(console_bindings);

    system_setup.system=&system;system_setup.diagnostics=&diagnostic_state;
    system_setup.diagnostic_services=console_bindings.diagnostic_services;system_setup.console_context=this;
    system_setup.register_console=[](void* p,const std::array<SystemConsoleDescriptor,7>&,const char*){
        return owner(p).console.register_component(CoreSetupStage::System)==ConsoleResult::Ok;};
    system_tick.context=this;
    system_tick.assertion=[](void* p,SystemTickAssertion value){return value==SystemTickAssertion::WatchdogReset?
        owner(p).assertion("System.h",0x106,"ESP_OK == ret"):
        owner(p).assertion("SystemState.h",0x2f,"LastDate <= now");};
    system_tick.log_maximum=[](void*,std::uint32_t timestamp,std::uint32_t maximum){
        return Serial.printf("\x1b[33m[W_][%8.2f][STAT][SystemState.h:56] DurationMax updated to %lu ms\x1b[0m\r\n",
            seconds(timestamp),static_cast<unsigned long>(maximum))>0;};
    ports.system_tick=system_tick_services(system_tick);
    core.eeprom.backend=arduino_eeprom_backend(EEPROM);core.eeprom.services=console.eeprom_services();

    wifi.context=this;wifi.manager=&core.wifi;
    wifi.emit=[](void* p,WifiEventType type,WifiError error,std::int32_t count){CoreWifiEvent event;event.type=type;
        event.argument=static_cast<std::uint32_t>(error);event.second_argument=static_cast<std::uint32_t>(count);
        owner(p).core.core.wifi_events.put(event);return true;};
    wifi.register_commands=[](void* p,WifiManager&){return owner(p).console.register_component(CoreSetupStage::Wifi)==ConsoleResult::Ok;};
    core.wifi.services=wifi_services(wifi);
    inputs.encoder=&core.encoder;core.encoder.io=physical_input_io(inputs);core.power_button.io=core.encoder.io;
    core.encoder.services.context=this;
    core.encoder.services.register_commands=[](void* p,PhysicalEncoder&){return owner(p).console.register_component(CoreSetupStage::Encoder)==ConsoleResult::Ok;};
    core.encoder.services.button_event=[](void* p,const CoreButtonEvent& value){owner(p).head.receive_button(value);return true;};
    core.encoder.services.rotation_event=[](void* p,std::int32_t value){owner(p).head.receive_encoder(value);return true;};
    buzzer.context=this;
    buzzer.begin_console=[](void* p,const char* name){return owner(p).console.begin_buzzer_console(name);};
    buzzer.add_command=[](void* p,Buzzer& value,const BuzzerCommandDefinition& definition){return owner(p).console.add_buzzer_command(value,definition);};
    buzzer.assertion=[](void* p,std::uint32_t line){return owner(p).assertion("System.h",line,"_state.IsAlarmOn == _buzzer.isEnabled()");};
    buzzer.diagnostic=[](void* p,BuzzerDiagnostic type,std::uint32_t timestamp,const BuzzerState& state,bool enabled){
        if(type==BuzzerDiagnostic::Information)return Serial.printf("[I_][%8.2f][BUZ][Buzzer.h:87] Enabled = %s, Freq = %d, Duty = %d\r\n",
            seconds(timestamp),state.enabled?"true":"false",state.frequency_hz,state.duty)>0;
        return Serial.printf("\x1b[33m[W_][%8.2f][SYS][System.h:282] IsAlarmOn = %s\x1b[0m\r\n",seconds(timestamp),boolean(enabled))>0;
    };
    core.buzzer.services=buzzer_services(buzzer);
    core.buzzer.services.enter=[](void* p){auto& a=owner(static_cast<BuzzerBindings*>(p)->context);xSemaphoreTakeRecursive(a.buzzer_mutex,portMAX_DELAY);};
    core.buzzer.services.leave=[](void* p){auto& a=owner(static_cast<BuzzerBindings*>(p)->context);xSemaphoreGiveRecursive(a.buzzer_mutex);};

    core.backend_http.services=http_services(backend_socket.http);core.session_http.services=http_services(session_socket.http);
    core.firmware_http.services=http_services(firmware_socket.http);core.binary_http.services=http_services(binary_socket.http);
    test_http_services=http_services(test_socket.http);test_http_services.diagnostics=diagnostics;
    still_transport.client=&still_socket;core.still_http.services=http_services(still_transport);
    still_services=still_http_services(core.still_binding);core.still_client.services.context=this;
    core.still_client.services.perform=[](void* p,const StillHttpAction& action,std::uint32_t& result){
        auto& a=owner(p);const bool ok=a.still_services.perform(a.still_services.context,action,result);
        if(ok&&result&&action.effect==StillHttpEffect::Configure)a.still_transport.client=&a.still_socket;
        return ok;
    };
    core.backend.authorization=core.sessions.authorization=authorization.authorization.authorization=original_credentials::http_authorization;
    azure.attach(core.azure);core.azure.key=original_credentials::azure_key;core.azure.key_size=sizeof original_credentials::azure_key;
    update.update=&Update;update.output=&Serial;update.diagnostics=diagnostics;
    core.firmware_download.services=firmware_update_services(update);local_http.bind(core.local_server);
    core.serial_logger.services.context=this;
    core.serial_logger.services.assertion=[](void* p,const char* file,std::uint32_t line,const char* expression){return owner(p).assertion(file,line,expression);};
    core.serial_logger.services.emit=[](void* p,SerialLogMessage type,const char* text,std::uint32_t value){return owner(p).serial_log(type,text,value);};
    ports.backend_scope.context=this;ports.backend_scope.emit=[](void* p,const char* tag,const char* file,std::uint32_t line,const char* name){
        auto& a=owner(p);return a.scope(true,tag,file,line,name)&&a.scope(false,tag,file,line,name);};
    pinger.registry_context=this;pinger.register_commands=[](void* p){return owner(p).console.register_component(CoreSetupStage::Pinger)==ConsoleResult::Ok;};
    console.hooks.context=this;
    console.hooks.invoke=[](void* p,std::uint32_t address,const char* value){return owner(p).invoke_console(address,value);};
    console.hooks.write=[](void*,const char* bytes,std::size_t count){return Serial.write(reinterpret_cast<const std::uint8_t*>(bytes),count)==count;};
    console.hooks.scope=[](void* p,bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){return owner(p).scope(begin,tag,file,line,name);};
    console.hooks.test_scope=[](void* p,bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){return owner(p).test_scope(begin,tag,file,line,name);};
    console.hooks.millis=clock_ms;
    console.hooks.assertion_count=[](void* p){return owner(p).diagnostic_state.assertion_count;};
    console.hooks.stack_high_water=[](void*){return static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(nullptr));};
    console.hooks.yield=[](void*){::yield();return true;};
    console.hooks.delay_ms=[](void*,std::uint32_t value){::delay(value);return true;};
    console.hooks.watchdog_reset=[](void*){esp_task_wdt_reset();return true;};
    console.hooks.http_test_services=&test_http_services;
    console.hooks.test_message=[](void* p,const char* value){return owner(p).display_console.test_message(value);};

    auto& h=head.services;h.context=this;h.millis=clock_ms;h.diagnostics=diagnostics;
    h.scope=console.hooks.test_scope; // UiSlideshow::StabilityText uses the original TestScope.
    h.assertion=[](void* p,const char* file,std::uint32_t line,const char* expression){return owner(p).assertion(file,line,expression);};
    h.post_core=[](void* p,const HeadUiCoreEnvelope& value){return owner(p).core.post(value);};
    h.alarm=[](void* p,bool enabled){auto& a=owner(p);return a.core.buzzer.set_alarm(a.core.alarm,enabled)==BuzzerResult::Ok;};
    h.check_rinse=[](void*,std::uint32_t type,std::uint32_t& result){result=type==6||type==7||type==8||type==9||type==12||type==13||type==14;return true;};
    h.save_configuration=[](void* p){return owner(p).save_configuration();};
    h.apply_azure_configuration=[](void* p){auto& c=owner(p).core;
        c.core.state.azure_enabled=c.eeprom.data.azure_enabled!=BoolOption::False;
        if(c.core.state.azure_enabled)return c.azure.recover(false)==BackendResult::Ok;
        c.azure.enabled=false;return true;};
    h.discard_remote_session=[](void* p){return owner(p).core.azure.session_discard()==BackendResult::Ok;};
    h.scan_network=[](void*,std::uint32_t index,HeadWifiNetwork& result){
        copy_text(result.ssid,WiFi.SSID(index).c_str());result.rssi=WiFi.RSSI(index);
        result.encrypted=WiFi.encryptionType(index)!=WIFI_AUTH_OPEN;return true;};
    h.run_display_tests=[](void* p){auto& d=owner(p).display_console;return d.test()&&d.text_modifier_test()&&d.graphics_test();};
    h.display_test=[](void* p){return owner(p).display_console.test();};
    h.random_below=[](void*,std::uint32_t limit){return static_cast<std::uint32_t>(::random(static_cast<long>(limit)));};
    h.register_commands=[](void* p,HeadApplication&,const std::array<HeadConsoleDescriptor,6>&){return owner(p).console.register_component(CoreSetupStage::Head)==ConsoleResult::Ok;};
    h.display_setup.context=this;
    h.display_setup.report_information=[](void* p){auto& a=owner(p);a.registration_ok=a.display_console.information()&&a.registration_ok;};
    h.display_setup.register_commands=[](void* p){auto& a=owner(p);a.registration_ok=a.console.register_display()==ConsoleResult::Ok&&a.registration_ok;};
    h.display_io=display_io(system.spi);h.display_delay.context=this;h.display_delay.milliseconds=[](void*,std::uint32_t value){::delay(value);};
    display_console.io=h.display_io;display_console.delay=h.display_delay;display_console.diagnostics=diagnostics;
    display_console.services.context=this;
    display_console.services.test_scope=[](void* p,bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){return owner(p).test_scope(begin,tag,file,line,name);};
}

bool FirmwareApplication::setup_component(CoreSetupStage stage){
    switch(stage){
    case CoreSetupStage::System:{
        SystemSetupServices services=system_setup_services(system_setup);services.context=this;
        services.perform=[](void* p,const SystemSetupAction& action,std::uintptr_t& result){
            auto& a=owner(p);if(action.effect==SystemSetupEffect::ScanBuses){result=0;return a.scan_buses();}
            const auto original=system_setup_services(a.system_setup);return original.perform(original.context,action,result);};
        services.assertion=[](void* p,std::uint32_t line,const char* expression){return owner(p).assertion("System.h",line,expression);};
        return setup_system(system_setup_state,services)==SystemSetupResult::Ok;
    }
    case CoreSetupStage::Head:observe_head();return head.setup()==HeadApplicationResult::Ok&&registration_ok;
    case CoreSetupStage::Pinger:return pinger.setup()==PingerResult::Ok;
    case CoreSetupStage::Information:return core_information();
    default:return console.register_component(stage)==ConsoleResult::Ok;
    }
}
bool FirmwareApplication::setup(){
    // Both the OEM entry point and an owned modified-profile composition use
    // this lifetime. Bind the ISR owner before setup installs any timers.
    active_application=this;
    return construction_ok&&core.setup()==ApplicationResult::Ok&&registration_ok&&console.root();
}
bool FirmwareApplication::tick(std::uint32_t now){
    if(timer_failure){const auto failure=timer_failure;timer_failure=0;
        if(!log(ConsoleLevel::Error,"CORE","application_platform.cpp",0,"Timer service failure %lu",static_cast<unsigned long>(failure)))return false;}
    return core.tick(now)==ApplicationResult::Ok;
}
void FirmwareApplication::observe_head(){
    core.refresh_observed();auto& o=head.observed;
    o.core=core.core.state;o.runtime=core.runtime.state;o.runtime_inputs=core.runtime.observed;o.session=core.commands.session;
    o.wifi=core.wifi.state;o.auxiliary=core.auxiliary;o.msp=core.msp_state;
    portENTER_CRITICAL(&thermal_lock);o.heater=core.heater.state;o.heater_sensors=core.temperatures;o.safety=core.safeties;portEXIT_CRITICAL(&thermal_lock);
    o.still=core.still.state;o.still_samples=core.still_samples;o.still_connectivity_error=core.still.state.polling_stopped;
    o.imperial=core.eeprom.data.use_imperial!=0;o.reference_pressure_pa=core.csv.reference_pressure_pa;
    o.backend.runtime=core.runtime.state.id;o.backend.download_received=static_cast<std::int32_t>(core.firmware_download.received);
    o.backend.download_total=static_cast<std::int32_t>(core.firmware_download.expected);
    o.backend.interrupted_sessions=core.backend_state.resumable_session_id;o.backend.maintenance_count=core.backend_state.dirty_sessions;
    o.backend.maintenance_date=static_cast<std::uint32_t>(core.backend_state.last_session);o.backend.maintenance_limit=3;
    o.backend.registration_token=core.backend_state.registration_token.text.data();o.backend.network_detail=core.wifi.state.requested.ssid.data();
    const auto& program=core.program.record();o.loaded_session_type=program.type;o.loaded_program_status=static_cast<std::uint32_t>(core.azure.state);
    o.loaded_recipe.id=program.unique_id;copy_text(o.loaded_recipe.name,program.view.name);
    o.loaded_recipe.name_length=std::strlen(o.loaded_recipe.name.data());o.loaded_recipe.parameters=core.program.parameters;
    o.loaded_duration=core.program.stats.duration_seconds;o.flash_bytes=ESP.getFlashChipSize();
    copy_text(o.backend_alias,core.backend_state.alias.text.data());copy_text(o.wifi_bssid,WiFi.BSSIDstr().c_str());
    o.network=core.network;o.ethernet_address=esp32::ethernet_address();o.ethernet_mac=core.ethernet_mac;o.wifi_mac=core.wifi.mac_address;
    o.resumed_session=core.commands.resumed_session;o.t1_monitor=core.runtime.state.t1_drop_watch.enabled;
    copy_text(o.error_text,core.system_error_text.data());o.sensor_error=core.core.observed.sensor_error;
    copy_text(o.backend_error_text,core.backend_state.error_text.text.data());o.backend_error_code=static_cast<std::uint32_t>(core.backend_state.backend_error);
    o.backend_error=core.backend_state.error;o.registered=core.backend_state.registered;o.identifier=core.identifier;
}

void IRAM_ATTR FirmwareApplication::heater_second(){
    auto* a=active_application;if(!a)return;portENTER_CRITICAL_ISR(&a->thermal_lock);
    const bool ok=heater_tick_1s(a->core.heater,a->core.temperatures,a->core.safeties.thermal,a->thermal_limits,::millis(),a->core.ports.heater_gpio);
    if(!ok)a->timer_failure=1;portEXIT_CRITICAL_ISR(&a->thermal_lock);
}
void IRAM_ATTR FirmwareApplication::heater_fast(){
    auto* a=active_application;if(!a)return;portENTER_CRITICAL_ISR(&a->thermal_lock);
    const bool ok=heater_tick_20ms(a->core.heater,a->core.ports.heater_gpio);
    if(!ok)a->timer_failure=2;portEXIT_CRITICAL_ISR(&a->thermal_lock);
}
void FirmwareApplication::buzzer_timer(TimerHandle_t){
    auto* a=active_application;if(a&&a->core.buzzer.tick()!=BuzzerResult::Ok)a->timer_failure=3;
}
bool FirmwareApplication::log(ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* format,...){
    char text[1024];va_list arguments;va_start(arguments,format);const int count=::vsnprintf(text,sizeof text,format,arguments);va_end(arguments);
    return count>=0&&static_cast<std::size_t>(count)<sizeof text&&diagnostics.log&&diagnostics.log(diagnostics.context,level,tag,file,line,text);
}
bool FirmwareApplication::assertion(const char* file,std::uint32_t line,const char* expression){
    return diagnostics.assertion&&diagnostics.assertion(diagnostics.context,file,line,expression);
}
bool FirmwareApplication::scope(bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){
    std::uint32_t elapsed=0;
    if(begin){if(scope_depth_==scopes_.size())return false;scopes_[scope_depth_++].started=::millis();}
    else{if(!scope_depth_)return false;elapsed=::millis()-scopes_[--scope_depth_].started;}
    return Serial.printf("[D%c][%8.2f][%s][%s:%u] %s %s %lu\r\n",begin?'>':'<',seconds(::millis()),tag,file,
        static_cast<unsigned>(line),name,begin?"BEGIN":"END  ",static_cast<unsigned long>(elapsed))>0;
}
bool FirmwareApplication::test_scope(bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){
    if(begin){
        if(test_depth_==tests_.size())return false;auto& frame=tests_[test_depth_++];frame.started=::millis();frame.assertions=diagnostic_state.assertion_count;
        const bool ok=Serial.printf("[T>][%8.2f][%s][%s:%u] [%s] BEGIN\r\n",seconds(frame.started),tag,file,static_cast<unsigned>(line),name)>0;
        esp_task_wdt_reset();return ok;
    }
    if(!test_depth_)return false;const auto frame=tests_[--test_depth_];const auto stack=uxTaskGetStackHighWaterMark(nullptr);
    if(stack<=700&&!assertion("Assert.h",0x48,"stackHighWaterMark > 700"))return false;
    const auto now=::millis();const auto count=diagnostic_state.assertion_count-frame.assertions;
    return Serial.printf("[T_][%8.2f][%s][%s:%u] [%s] %s %d %lums\r\n",seconds(now),tag,file,
        static_cast<unsigned>(line),name,count?"FAIL":"OK",static_cast<std::int32_t>(count),static_cast<unsigned long>(now-frame.started))>0&&
        log(ConsoleLevel::Warning,"SYST","Assert.h",0x4f,"StackHighWaterMark = %lu",static_cast<unsigned long>(stack))&&
        log(ConsoleLevel::Info,"SYST",nullptr,0,"StackHighWaterMark = %lu",static_cast<unsigned long>(stack));
}
bool FirmwareApplication::serial_log(SerialLogMessage type,const char* text,std::uint32_t value){
    switch(type){
    case SerialLogMessage::Header:return log(ConsoleLevel::Info,"CSTA",nullptr,0,"CSVHDR = %s",text);
    case SerialLogMessage::Status:return log(ConsoleLevel::Info,"CSTA","CoreHelpers.h",0x42,"%s",text);
    case SerialLogMessage::Enabled:return log(ConsoleLevel::Info,"LOG",nullptr,0,"_isLogging = %s",text);
    case SerialLogMessage::Interval:return log(ConsoleLevel::Info,"LOG",nullptr,0,"_intervalMs = %lu",static_cast<unsigned long>(value));
    case SerialLogMessage::BufferTruncated:return log(ConsoleLevel::Warning,"BUF","Buffer.h",value,"Buffer truncated");
    case SerialLogMessage::DroppedBytes:return log(ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,"Dropping %d bytes",static_cast<std::int32_t>(value));
    case SerialLogMessage::IntervalBelowMinimum:return log(ConsoleLevel::Error,"LOG","CoreHelpers.h",0x75,"Interval below minimum");
    }return false;
}
bool FirmwareApplication::screen_power(bool enabled){
    if(!log(ConsoleLevel::Debug,"CORE","Core.h",0x10a,"screenPower() %s",boolean(enabled)))return false;
    digitalWrite(33,enabled?HIGH:LOW);return true;
}
bool FirmwareApplication::restart(){if(!log(ConsoleLevel::Warning,"SYST","System.h",0x112,"restart()"))return false;ESP.restart();return true;}
bool FirmwareApplication::select_factory(){
    if(!log(ConsoleLevel::Warning,"SYST","System.h",0x10c,"factoryReset()"))return false;
    esp_ota_set_boot_partition(system.factory_partition);return true;
}
bool FirmwareApplication::save_configuration(){
    if(!log(ConsoleLevel::Warning,"CORE","Core.h",0x120,"_eepromSave")||!set_core_heater_timers(core.timers,false,core.ports.timers))return false;
    const auto result=core.eeprom.write();const bool enabled=set_core_heater_timers(core.timers,true,core.ports.timers);return result==EepromResult::Ok&&enabled;
}
bool FirmwareApplication::ethernet_connect(std::uint32_t timeout){
    if(!scope(true,"SYST","System.h",0x131,"ethernetConnect"))return false;
    const int result=connect_ethernet(system.spi,core.ethernet_mac,timeout);
    const bool reported=log(ConsoleLevel::Info,"SYST",nullptr,0,"Ethernet.begin = %d",result)&&
        report_ip(*this,"SYST","Ethernet.localIP()",esp32::ethernet_address());
    return scope(false,"SYST","System.h",0x131,"ethernetConnect")&&reported;
}

// Remaining platform information/console definitions are kept in one included
// implementation file to keep the ownership and transport wiring readable.
#include "application_platform_console.inc"

namespace {
void firmware_task(void*){
    initialize_console();PingerBinding::initialize_callback();
    void* memory=heap_caps_malloc(sizeof(FirmwareApplication),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!memory){Serial.println("Z119 application allocation failed");vTaskDelete(nullptr);return;}
    auto* app=new(memory)FirmwareApplication;active_application=app;
    if(!app->setup()){
        app->log(ConsoleLevel::Error,"CORE","application_platform.cpp",0,"Application setup failed: stage=%u detail=%lu",
            static_cast<unsigned>(app->core.status.failed_setup),static_cast<unsigned long>(app->core.status.detail));
        // Missing required bindings cannot become a running firmware instance.
        // Keep the task alive to report the diagnostic, without starting ticks.
        for(;;){esp_task_wdt_reset();::delay(1000);}
    }
    for(;;){::yield();app->tick(::millis());}
}
}
bool start_firmware(){return start_core_task(firmware_task,nullptr);}
}}} // namespace zseries::oem::esp32
