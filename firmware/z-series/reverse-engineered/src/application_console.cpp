#include "zseries/application_console.hpp"
#include "zseries/core_application.hpp"
#include "zseries/backend_console.hpp"
#include "zseries/backend_console_tests.hpp"
#include "zseries/basic_console_tests.hpp"
#include "zseries/network_console_tests.hpp"
#include "zseries/hardware_console_tests.hpp"
#include "zseries/software_console_tests.hpp"
#include "zseries/msp_console_tests.hpp"
#include "zseries/msp_images.hpp"
#include "zseries/head_dynamic.hpp"
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace zseries { namespace oem {
namespace {
#include "application_console_manifest.inc"
const char* boolean(bool value) { return value ? "True" : "False"; }
std::int32_t integer(const char* value) {
    // The pinned newlib atoi saturates at signed 32-bit limits.
    const auto parsed=::strtoll(value?value:"",nullptr,10);
    return static_cast<std::int32_t>(std::max<long long>(std::numeric_limits<std::int32_t>::min(),
        std::min<long long>(std::numeric_limits<std::int32_t>::max(),parsed)));
}
template<std::size_t N> void text(std::array<char,N>& destination,const char* value) {
    destination.fill(0);if(value)std::memcpy(destination.data(),value,std::min(N-1,std::strlen(value)));
}
struct ControlGuard {
    const HeaterSynchronization& control;
    explicit ControlGuard(const HeaterSynchronization& value):control(value){if(control.enter&&control.leave)control.enter(control.context);}
    ~ControlGuard(){if(control.enter&&control.leave)control.leave(control.context);}
};
}

struct ApplicationConsole::Implementation {
    struct Node {std::unique_ptr<ConsoleEntry[]> entries;std::unique_ptr<ConsoleShell> shell;};
    struct Binding {Implementation* owner=nullptr;std::uint32_t callback=0;};
    ApplicationConsole& owner;CoreApplication& app;ConsoleServices services;
    std::array<Node,static_cast<unsigned>(ShellId::Count)> nodes;
    std::array<Binding,RegistryEntryCount> bindings;
    std::array<bool,RegistryEntryCount> installed{};
    bool eeprom_report_succeeded=false;
    bool heater_gains_observed=false;
    double heater_kp=0,heater_ki=0,heater_kd=0;
    Implementation(ApplicationConsole& shell,CoreApplication& value,const ConsoleServices& logs):owner(shell),app(value),services(logs){}
    ConsoleShell* shell(ShellId id) {
        const auto index=static_cast<unsigned>(id);if(index>=nodes.size())return nullptr;
        auto& node=nodes[index];if(node.shell)return node.shell.get();
        const auto& definition=shell_definitions[index];
        node.entries.reset(new(std::nothrow) ConsoleEntry[definition.capacity]);
        if(!node.entries)return nullptr;
        node.shell.reset(new(std::nothrow) ConsoleShell(definition.name,node.entries.get(),definition.capacity,services));
        return node.shell.get();
    }
    ConsoleResult add(std::size_t index) {
        if(installed[index])return ConsoleResult::Ok;
        const auto& definition=registry_entries[index];auto* target=shell(definition.shell);
        if(!target)return ConsoleResult::ServiceFailure;
        ConsoleResult result;
        if(definition.child!=ShellId::None){auto* child=shell(definition.child);if(!child)return ConsoleResult::ServiceFailure;result=target->add_child(definition.token,child);}
        else {auto& binding=bindings[index];binding.owner=this;binding.callback=definition.callback;
            result=target->add_command(definition.exact,definition.token,definition.description,&binding,[](void* context,const char* argument){
                auto& value=*static_cast<Binding*>(context);return value.owner->invoke(value.callback,argument);});}
        if(result==ConsoleResult::Ok)installed[index]=true;
        return result;
    }
    ConsoleResult register_group(RegistryId id) {
        for(std::size_t i=0;i<RegistryEntryCount;++i)if(registry_entries[i].registry==id){const auto result=add(i);if(result!=ConsoleResult::Ok)return result;}
        return ConsoleResult::Ok;
    }
    bool log(ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* format,...) {
        if(!services.log)return false;
        char message[512];va_list args;va_start(args,format);const auto count=::vsnprintf(message,sizeof(message),format,args);va_end(args);
        return count>=0&&static_cast<std::size_t>(count)<sizeof(message)&&services.log(services.context,level,tag,file,line,message);
    }
    bool assertion(const char* file,std::uint32_t line,const char* expression) {
        return services.assertion&&services.assertion(services.context,file,line,expression);
    }
    bool platform(std::uint32_t id,const char* argument) {return owner.hooks.invoke&&owner.hooks.invoke(owner.hooks.context,id,argument);}
    bool information_eeprom(const EepromData& value);
    bool information_backend();
    bool information_heater();
    bool information_safeties();
    bool information_session();
    bool information_msp();
    bool information_adc();
    bool information_runtime();
    bool information_still();
    bool information_still_sensors();
    bool detect_adc();
    bool send_still(Command command) {
        const auto state=static_cast<std::uint32_t>(app.still.state.id);
        if(state!=15&&state!=17)return log(ConsoleLevel::Error,"STILLC","PicoStill.h",0x4c3,"Cannot send command in state %s",to_string(app.still.state.id));
        return app.still_client.command(command)==StillClientResult::Ok;
    }
    bool invoke(std::uint32_t,const char*);
    bool msp_send(std::uint8_t command,std::uint32_t value=0,unsigned bytes=0) {return z_msp_send(&app.msp,command,value,bytes)==Z_MSP_OK;}
    bool msp_servo(std::uint8_t command,std::uint8_t value) {return log(ConsoleLevel::Warning,"MSP","Msp.h",0x347,"SERVO %d %d",command,value)&&msp_send(command,value,1);}
    bool stop_safety(TimedThresholdMonitor& monitor,bool& tripped,const char* name) {
        bool enabled;{ControlGuard guard(app.ports.control);enabled=monitor.enabled;}
        if(!enabled)return true;
        if(!log(ConsoleLevel::Debug,"SFTY","Safeties.h",0x40,"stop \"%s\"",name))return false;
        ControlGuard guard(app.ports.control);stop_monitor(monitor,tripped);return true;
    }
    bool still_command(StillCommandId id,std::int32_t selection=-1) {PicoStillCommand value;value.id=id;value.selection=selection;app.still.commands.put(value);return true;}
    bool runtime_command(RuntimeCommandId id,std::uint32_t program=0,double temperature=0) {RuntimeCommand value;value.id=id;value.program_id=program;value.temperature=temperature;return app.post_runtime(value);}
    bool wifi_target(WiFiStateId value) {
        if(!log(ConsoleLevel::Info,"WIFI","Wifi.h",0x32,"Target = %s",to_string(value)))return false;
        app.wifi.target=value;
        if(!log(ConsoleLevel::Info,"BTMR","Timer.h",0x74,"%s - Stopping BackoffTimer","Wifi.h"))return false;
        app.wifi.backoff.stop();return true;
    }
    template<std::size_t N> bool wifi_text(std::array<char,N>& destination,const char* value) {
        text(destination,value);const auto size=value?std::strlen(value):0;
        return size<N||(log(ConsoleLevel::Warning,"BUF","Buffer.h",0x66,"Buffer truncated")&&
            log(ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,"Dropping %d bytes",static_cast<int>(size-(N-1))));
    }
    void read_gains() {if(!heater_gains_observed){ControlGuard guard(app.ports.control);heater_kp=app.heater.pid.display_kp;heater_ki=app.heater.pid.display_ki;heater_kd=app.heater.pid.display_kd;heater_gains_observed=true;}}
};

bool ApplicationConsole::Implementation::information_eeprom(const EepromData& value) {
    const auto info=ConsoleLevel::Info;const auto tag="EPRM";
    return log(info,tag,nullptr,0,"Version = %lu",static_cast<unsigned long>(value.version))&&
        log(info,tag,nullptr,0,"UseEthernet = %s",boolean(value.use_ethernet))&&
        log(info,tag,nullptr,0,"UseImperial = %s",boolean(value.use_imperial))&&
        log(info,tag,nullptr,0,"ShowDetail = %s",boolean(value.show_detail))&&
        log(info,tag,nullptr,0,"SlideshowCycleStatic = %s",boolean(value.slideshow_cycle_static))&&
        log(info,tag,nullptr,0,"Language = %s",to_string(value.language))&&
        log(info,tag,nullptr,0,"StillSessionMode = %s",to_string(value.still_session_mode))&&
        log(info,tag,nullptr,0,"Boiler = %s",to_string(value.boiler))&&
        log(info,tag,nullptr,0,"SSID = %s",value.wifi_ssid[0]?value.wifi_ssid.data():"<EMPTY>")&&
        log(info,tag,nullptr,0,"Password = %s",value.wifi_password[0]?"*":"<EMPTY>")&&
        log(info,tag,nullptr,0,"AzureEnabled = %s",to_string(value.azure_enabled));
}
bool ApplicationConsole::Implementation::information_backend() {
    const auto& value=app.backend_state;const auto info=ConsoleLevel::Info;const auto tag="BEND";
    return log(info,tag,nullptr,0,"IsSyncDone = %s",boolean(value.ready))&&
        log(info,tag,nullptr,0,"UpdateAddress = %s",value.update_address.text.data())&&
        log(info,tag,nullptr,0,"BinaryAddress = %s",value.program_uri.text.data())&&
        log(info,tag,nullptr,0,"RegistrationCode = %s",value.registration_token.text.data())&&
        log(info,tag,nullptr,0,"IsCodeExpired = %s",boolean(value.token_expired))&&
        log(info,tag,nullptr,0,"IsRegistered = %s",boolean(value.registered))&&
        log(info,tag,nullptr,0,"IsUidKnown = %s",boolean(value.byte_146))&&
        log(info,tag,nullptr,0,"ClientError = %s",value.error_text.text.data())&&
        log(info,tag,nullptr,0,"BackendError = %s",to_string(value.backend_error))&&
        log(info,tag,nullptr,0,"BackendErrorINT = %d",static_cast<int>(value.backend_error))&&
        log(info,tag,nullptr,0,"Alias = %s",value.alias.text.data())&&
        log(info,tag,nullptr,0,"DirtySessionsSinceClean = %d",value.dirty_sessions)&&
        log(info,tag,nullptr,0,"LastSessionType = %s",to_string(value.last_session));
}
bool ApplicationConsole::Implementation::information_heater() {
    read_gains();HeaterState state;{ControlGuard guard(app.ports.control);state=app.heater.state;}
    const auto info=ConsoleLevel::Info;const auto tag="HEAT";
    return log(info,tag,nullptr,0,"PID = Kp=%.1f Ki=%.1f Kd=%.1f",heater_kp,heater_ki,heater_kd)&&
        log(info,tag,nullptr,0,"10kp = %lu",static_cast<unsigned long>(static_cast<std::uint32_t>(static_cast<std::int32_t>(heater_kp*10))))&&
        log(info,tag,nullptr,0,"10ki = %lu",static_cast<unsigned long>(static_cast<std::uint32_t>(static_cast<std::int32_t>(heater_ki*10))))&&
        log(info,tag,nullptr,0,"10kd = %lu",static_cast<unsigned long>(static_cast<std::uint32_t>(static_cast<std::int32_t>(heater_kd*10))))&&
        log(info,tag,nullptr,0,"ITerm = %lu",static_cast<unsigned long>(static_cast<std::uint32_t>(static_cast<std::int32_t>(state.integral_display))))&&
        log(info,tag,nullptr,0,"isEnabled = %s",boolean(state.enabled))&&log(info,tag,nullptr,0,"isStarted = %s",boolean(state.started));
}
bool ApplicationConsole::Implementation::information_safeties() {
    SafetyState value;{ControlGuard guard(app.ports.control);value=app.safeties;}
    const auto info=ConsoleLevel::Info;const auto file="SafetiesState.h";const auto tag="SAFE";
    return log(info,tag,file,0x26,"%8s 0x%0x","tooHot",value.thermal.too_hot)&&
        log(info,tag,file,0x27,"%8s 0x%0x","tooCold",value.thermal.too_cold)&&
        log(info,tag,file,0x28,"%8s 0x%0x","tooOld",value.thermal.too_old)&&
        log(info,tag,file,0x29,"%s %s","hasMaxWortDrainDelta",boolean(value.max_wort_drain_delta))&&
        log(info,tag,file,0x2a,"%s %s","hasMaxHexRise",boolean(value.max_hex_rise))&&
        log(info,tag,file,0x2b,"%s %s","hasMaxHexWortDelta",boolean(value.max_hex_wort_delta))&&
        log(info,tag,file,0x2c,"%s %s","isStepFilterMissing",boolean(value.filter_missing));
}

bool ApplicationConsole::Implementation::invoke(std::uint32_t id,const char* argument) {
    const auto value=integer(argument);
    // Numeric and text setters retain the OEM atoi, byte narrowing and
    // drop-new mailbox behavior; SDK operations still require their bindings.
    switch(id) {
    case 0x400e6bbc:
        if(!app.wifi.state.requested.ssid[0]&&!assertion("Wifi.h",0x133,"creds.Ssid.length() > 0"))return false;
        return !app.wifi.state.requested.ssid[0]||wifi_target(WiFiStateId::Connected);
    case 0x400d5c20:return wifi_target(WiFiStateId::Scanning);
    case 0x400d5c10:return wifi_target(WiFiStateId::AccessPoint);
    case 0x400d5c30:return wifi_target(WiFiStateId::Idle);
    case 0x400f2040:return wifi_text(app.wifi.state.requested.ssid,argument);
    case 0x40101088:return wifi_text(app.wifi.state.requested.password,argument);
    case 0x400d0f08:app.wifi.state.requested.internet_expected=eeprom_text_boolean(argument);return true;
    case 0x400dafd8:return log(ConsoleLevel::Info,"HSERV",nullptr,0,"isRunning = %s",boolean(app.local_server.enabled));
    case 0x400d5d14:return app.local_server.enable(true)==LocalHttpResult::Ok;
    case 0x400d5d24:return app.local_server.enable(false)==LocalHttpResult::Ok;
    case 0x400d5d40:return app.buzzer.command(BuzzerCommand::Information,argument)==BuzzerResult::Ok;
    case 0x400d144c:return app.buzzer.command(BuzzerCommand::On,argument)==BuzzerResult::Ok;
    case 0x400d1460:return app.buzzer.command(BuzzerCommand::Off,argument)==BuzzerResult::Ok;
    case 0x400d08e0:return app.buzzer.command(BuzzerCommand::Frequency,argument)==BuzzerResult::Ok;
    case 0x400d08f4:return app.buzzer.command(BuzzerCommand::Duty,argument)==BuzzerResult::Ok;
    case 0x400d6750:return information_eeprom(app.eeprom.data);
    case 0x400d59ac:return app.eeprom.command(EepromCommand::Ssid,argument)==EepromResult::Ok;
    case 0x400d59c0:return app.eeprom.command(EepromCommand::Password,argument)==EepromResult::Ok;
    case 0x400d6760:return app.eeprom.command(EepromCommand::Azure,argument)==EepromResult::Ok;
    case 0x400d67a8:return app.eeprom.command(EepromCommand::Ethernet,argument)==EepromResult::Ok;
    case 0x401bdebc:return app.eeprom.command(EepromCommand::BoilerUnknown)==EepromResult::Ok;
    case 0x401bdee0:return app.eeprom.command(EepromCommand::BoilerBig)==EepromResult::Ok;
    case 0x401bdf04:return app.eeprom.command(EepromCommand::BoilerSmall)==EepromResult::Ok;
    case 0x400dd678:case 0x400de068:return hardware_console_test(id,app,services,owner.hooks);
    case 0x400e0e70:case 0x4011a204:case 0x4011a1fc:return software_console_test(id,app,services,owner.hooks);
    case 0x40119378:case 0x4011ec38:return backend_console_test(id,app,services,owner.hooks);
    case 0x4011ed44:case 0x4011fbac:return basic_console_test(id,app,services,owner.hooks);
    case 0x4011bffc:case 0x4011c2f8:case 0x4010a364:case 0x4010a3d8:case 0x4011fbbc:
        return core_console_test(id,app,services,owner.hooks);
    case 0x400dd278:return app.ports.msp_image&&msp_console_self_test(0x400dd29c,app.msp,*app.ports.msp_image,unit_test_msp_image,services,owner.hooks);
    case 0x400f068c:app.encoder.console_press();return true;
    case 0x400d19ac:case 0x400d19bc:case 0x400d19cc:case 0x400d19dc:return app.encoder.console_rotate(-1-static_cast<int>((id-0x400d19ac)/16))==PhysicalInputResult::Ok;
    case 0x400d19ec:case 0x400d19fc:case 0x400d1a0c:case 0x400d1a1c:return app.encoder.console_rotate(1+static_cast<int>((id-0x400d19ec)/16))==PhysicalInputResult::Ok;
    case 0x400d69b0:return information_safeties();
    case 0x401bf96c:{ControlGuard guard(app.ports.control);app.safeties.thermal.enabled=true;return true;}
    case 0x400d6a10:{{ControlGuard guard(app.ports.control);app.safeties.thermal.enabled=false;}
        return stop_safety(app.safety_controller.hex_wort_delta,app.safeties.max_hex_wort_delta,"hexWortDeltaMonitor")&&
            stop_safety(app.safety_controller.wort_drain_delta,app.safeties.max_wort_drain_delta,"drainWortDeltaMonitor")&&
            stop_safety(app.safety_controller.hex_rise,app.safeties.max_hex_rise,"hexTempMonitor");}
    case 0x400d6a80:case 0x400d6a90:{const bool missing=id==0x400d6a90;{ControlGuard guard(app.ports.control);app.safeties.filter_missing=missing;}
        return log(ConsoleLevel::Warning,"SFTY","Safeties.h",0x8b,"isStepFilterMissing = %s",missing?"true":"false");}
    case 0x400d6d64:return information_heater();
    case 0x400d6d8c:{double wort;{ControlGuard guard(app.ports.control);wort=app.temperatures.temperatures[1].celsius;}heater_start(app.heater,wort);return information_heater();}
    case 0x400d1a9c:{double wort;{ControlGuard guard(app.ports.control);wort=app.temperatures.temperatures[1].celsius;}return heater_stop(app.heater,app.ports.heater_gpio,wort);}
    case 0x400d1a54:return heater_enable(app.heater,app.ports.heater_gpio,true);
    case 0x400d1a64:return heater_enable(app.heater,app.ports.heater_gpio,false);
    case 0x400d6f10:case 0x400d6f30:{const double requested=id==0x400d6f10?static_cast<double>(value):(static_cast<double>(value)-32.0)/1.8;
        double maximum;{ControlGuard guard(app.ports.control);maximum=app.temperatures.estimated_boil_celsius-2.0;}
        if(requested>maximum&&!log(ConsoleLevel::Warning,"HEAT","Heater.h",0x91,"Target temp (%5.2fC) exceeds max (%5.2fC)",requested,maximum))return false;
        return heater_set_target(app.heater,requested,maximum+2.0);}
    case 0x400d096c:case 0x400d0980:{ControlGuard guard(app.ports.control);pid_set_mode(app.heater.pid,id==0x400d096c,app.temperatures.temperatures[1].celsius,app.heater.state.output_ticks);return true;}
    case 0x400de1b0:{bool automatic;{ControlGuard guard(app.ports.control);automatic=app.heater.pid.automatic;}
        if(automatic&&!assertion("Heater.h",0x4f,"MANUAL == _pid.GetMode()"))return false;
        if((value<0||value>100)&&!assertion("Heater.h",0x50,"0.0 <= duty && duty <= 100.0"))return false;
        ControlGuard guard(app.ports.control);app.heater.state.output_ticks=std::max(0,std::min(100,value));return true;}
    case 0x400d6e30:case 0x400d6d98:case 0x400d6de0:{read_gains();const double gain=static_cast<double>(value)/10.0;
        if(id==0x400d6e30)heater_kp=gain;else if(id==0x400d6d98)heater_ki=gain;else heater_kd=gain;
        {ControlGuard guard(app.ports.control);pid_set_tunings(app.heater.pid,heater_kp,heater_ki,heater_kd);}return information_heater();}
    case 0x401bdcdc:app.acquisition.adc.polling_enabled=true;return true;
    case 0x401bdd00:app.acquisition.adc.polling_enabled=false;return true;
    case 0x400dd8f4:return information_adc();
    case 0x400dd9a4:{const auto result=max11613_poll(app.acquisition.adc,app.ports.sensor_bus,&services);return (result==AcquisitionResult::Ok||result==AcquisitionResult::ShortReply||result==AcquisitionResult::ReadFailure)&&information_adc();}
    case 0x400ddeb8:return detect_adc();
    case 0x400f4f08:return still_command(StillCommandId::Scan);
    case 0x400f4f18:return still_command(StillCommandId::Connect,value);
    case 0x400f4f34:return still_command(StillCommandId::Start);
    case 0x400f4f8c:return still_command(StillCommandId::Stop);
    case 0x4010c9f4:return send_still(static_cast<Command>(6));
    case 0x4010ca04:return send_still(static_cast<Command>(7));
    case 0x4010ca14:return send_still(static_cast<Command>(10));
    case 0x4010ca24:return send_still(static_cast<Command>(9));
    case 0x4010ca34:return send_still(static_cast<Command>(11));
    case 0x40102710:app.still.state.error.fill(0);app.still_samples.current.error.fill(0);return information_still_sensors();
    case 0x400dab5c:return information_still();
    case 0x400f4a58:case 0x400f4a78:{double wort;{ControlGuard guard(app.ports.control);wort=app.temperatures.temperatures[1].celsius;}
        if(!heater_stop(app.heater,app.ports.heater_gpio,wort)||!msp_send(Z_MSP_PUMPS_SET,app.msp_state.pumps&0x40,1))return false;
        return runtime_command(id==0x400f4a58?RuntimeCommandId::Stop:RuntimeCommandId::Pause);}
    case 0x400f4a84:return runtime_command(RuntimeCommandId::Resume);
    case 0x400f4a94:return runtime_command(RuntimeCommandId::Skip);
    case 0x400f4aa4:return runtime_command(RuntimeCommandId::Drain);
    case 0x400f4adc:case 0x400f4b00:return runtime_command(RuntimeCommandId::SetTemperature,0,
        static_cast<double>(static_cast<float>(id==0x400f4adc?static_cast<double>(value):(static_cast<double>(value)-32.0)/1.8)));
    case 0x400f4b4c:return runtime_command(RuntimeCommandId::Start,1);
    case 0x400f4b5c:return runtime_command(RuntimeCommandId::Start,12);
    case 0x400f4b6c:return runtime_command(RuntimeCommandId::Start,2);
    case 0x400f4b7c:return runtime_command(RuntimeCommandId::Start,3);
    case 0x400f4b8c:return runtime_command(RuntimeCommandId::Start,4);
    case 0x400f4b9c:return runtime_command(RuntimeCommandId::Start,6);
    case 0x400f4bac:return runtime_command(RuntimeCommandId::Start,8);
    case 0x400f4bbc:return runtime_command(RuntimeCommandId::Start,9);
    case 0x400f4bcc:return runtime_command(RuntimeCommandId::Start,10);
    case 0x400f4bdc:return runtime_command(RuntimeCommandId::Start,11);
    case 0x400dafbc:return drop_watch_start(app.runtime.state.t1_drop_watch,1,&services)==RuntimeResult::Ok;
    case 0x400daf4c:return drop_watch_stop(app.runtime.state.t1_drop_watch,&services)==RuntimeResult::Ok;
    case 0x400e0194:return information_runtime();
    case 0x400daa1c:return information_backend();
    case 0x400dae28:return log(ConsoleLevel::Info,"ZBEND","ZBackend.h",0x186,"_pendingCmd = %d",static_cast<std::int32_t>(app.sessions.state));
    case 0x400df3b0:return information_session();
    case 0x401bdc88:app.session_logger.stop_requested=true;return true;
    case 0x4011c794:return app.sessions.log(app.session_log_snapshot())==BackendResult::Ok;
    case 0x400d76ec:return app.serial_logger.set_interval(argument)==SerialLogResult::Ok;
    case 0x400efbf4:return app.serial_logger.information()==SerialLogResult::Ok;
    case 0x400efac4:case 0x400efae4:return app.serial_logger.enable(true)==SerialLogResult::Ok;
    case 0x400efad4:case 0x400efaf4:return app.serial_logger.enable(false)==SerialLogResult::Ok;
    case 0x400edacc:return app.azure.connect()==BackendResult::Ok;
    case 0x401bedf8:app.azure.disconnect();return true;
    case 0x400eda7c:return app.azure.session_begin()==BackendResult::Ok;
    case 0x400edaa0:return app.azure.session_end()==BackendResult::Ok;
    case 0x400d0a5c:return app.azure.publish_state()==BackendResult::Ok;
    case 0x400f3ef4:{CoreCommand command;command.id=CoreCommandId::PowerButton;return app.post(command);}
    case 0x40101158:case 0x40101168:{CoreCommand command;command.id=id==0x40101158?CoreCommandId::WiFiConnect:CoreCommandId::EthernetConnect;return app.post(command);}
    case 0x400f0450:case 0x400f0460:return app.emit(id==0x400f0450?CoreEventType::RinseNeeded:CoreEventType::CleanNeeded);
    case 0x401040f4:{AzureCommand command;command.id=CoreCommandId::AzureSessionLoad;command.session.type=static_cast<SessionType>(3);
        command.session.program=static_cast<ZProgramId>(3);text(command.session.name,"TEST foo");return app.post_azure(command);}
    default:break;
    }
#include "application_console_msp.inc"
    return platform(id,argument);
}

#include "application_console_information.inc"

ApplicationConsole::ApplicationConsole(CoreApplication& app,const ConsoleServices& services) noexcept:
    implementation_(new(std::nothrow) Implementation(*this,app,services)){}
ApplicationConsole::~ApplicationConsole()=default;
ConsoleResult ApplicationConsole::register_component(CoreSetupStage stage) {
    if(!implementation_)return ConsoleResult::ServiceFailure;
    RegistryId registry;
    switch(stage) {
    case CoreSetupStage::Wifi:registry=RegistryId::Wifi;break;
    case CoreSetupStage::HttpServer:registry=RegistryId::HttpServer;break;
    case CoreSetupStage::Buzzer:registry=RegistryId::Buzzer;break;
    case CoreSetupStage::System:registry=RegistryId::System;break;
    case CoreSetupStage::Msp:registry=RegistryId::Msp;break;
    case CoreSetupStage::Eeprom:registry=RegistryId::Eeprom;break;
    case CoreSetupStage::Safeties:registry=RegistryId::Safeties;break;
    case CoreSetupStage::PicoStill:registry=RegistryId::PicoStill;break;
    case CoreSetupStage::Head:registry=RegistryId::Head;break;
    case CoreSetupStage::Encoder:registry=RegistryId::Encoder;break;
    case CoreSetupStage::ZBackend:registry=RegistryId::ZBackend;break;
    case CoreSetupStage::Runtime:registry=RegistryId::Runtime;break;
    case CoreSetupStage::HttpBackend:registry=RegistryId::HttpBackend;break;
    case CoreSetupStage::SerialLogger:registry=RegistryId::SerialLogger;break;
    case CoreSetupStage::SessionLogger:registry=RegistryId::SessionLogger;break;
    case CoreSetupStage::Azure:registry=RegistryId::Azure;break;
    case CoreSetupStage::TimerMonitor:registry=RegistryId::Pinger;break;
    case CoreSetupStage::Diagnostics:registry=RegistryId::Diagnostics;break;
    case CoreSetupStage::PowerButton:case CoreSetupStage::Accelerometer:case CoreSetupStage::Pressure:case CoreSetupStage::Information:
        return ConsoleResult::Ok; // Original stages contain no console registry.
    default:return ConsoleResult::InvalidInput;
    }
    return implementation_->register_group(registry);
}
ConsoleResult ApplicationConsole::register_display(){return implementation_?implementation_->register_group(RegistryId::Display):ConsoleResult::ServiceFailure;}
ConsoleResult ApplicationConsole::register_heater(){return implementation_?implementation_->register_group(RegistryId::Heater):ConsoleResult::ServiceFailure;}
ConsoleResult ApplicationConsole::register_max11613(){return implementation_?implementation_->register_group(RegistryId::Max11613):ConsoleResult::ServiceFailure;}
ConsoleResult ApplicationConsole::dispatch(const char* input){auto* main=root();return main?main->dispatch(input):ConsoleResult::ServiceFailure;}
ConsoleResult ApplicationConsole::invoke_original(std::uint32_t callback,const char* argument){
    if(!implementation_)return ConsoleResult::ServiceFailure;
    if(!argument)return ConsoleResult::InvalidInput;
    return implementation_->invoke(callback,argument)?ConsoleResult::Ok:ConsoleResult::ServiceFailure;
}
ConsoleShell* ApplicationConsole::root() noexcept{return implementation_?implementation_->nodes[static_cast<unsigned>(ShellId::Main)].shell.get():nullptr;}
bool ApplicationConsole::begin_buzzer_console(const char* name){return implementation_&&name&&std::strcmp(name,"Buzzer")==0&&implementation_->shell(ShellId::Buzzer);}
bool ApplicationConsole::add_buzzer_command(Buzzer& buzzer,const BuzzerCommandDefinition& value){
    if(!implementation_||&buzzer!=&implementation_->app.buzzer)return false;
    for(std::size_t i=0;i<RegistryEntryCount;++i){const auto& row=registry_entries[i];
        if(row.registry==RegistryId::Buzzer&&std::strcmp(row.token,value.name)==0&&row.exact==value.exact_match&&std::strcmp(row.description,value.description)==0)
            return implementation_->add(i)==ConsoleResult::Ok;}
    return false;
}
EepromServices ApplicationConsole::eeprom_services() noexcept {
    EepromServices result;result.context=this;
    result.diagnostics=implementation_?&implementation_->services:nullptr;
    result.self_test=[](void* context){auto* self=static_cast<ApplicationConsole*>(context)->implementation_.get();
        return self&&eeprom_console_self_test(self->services,self->owner.hooks);};
    result.register_commands=[](void* context,EepromStore&,const std::array<EepromCommandDescriptor,9>&){
        static_cast<ApplicationConsole*>(context)->register_component(CoreSetupStage::Eeprom);};
    result.report_data=[](void* context,const EepromData& data){auto* self=static_cast<ApplicationConsole*>(context)->implementation_.get();
        if(self)self->eeprom_report_succeeded=self->information_eeprom(data);};
    return result;
}
}} // namespace zseries::oem
