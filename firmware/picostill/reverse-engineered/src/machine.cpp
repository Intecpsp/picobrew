#include "picostill/machine.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <stdio.h>
namespace picostill { namespace oem {
Machine::Machine(HardwarePorts& hardware_ports,I2cPorts& i2c,DiagnosticPorts& diagnostic_ports,StoragePorts& storage_ports,
                 WifiPorts& wifi_ports,HttpRuntime& runtime,HttpListener& listener,OutboundClient& backend_client,
                 OutboundClient& update_client,UpdatePorts& update_ports,ReportPorts& report_ports,SystemPorts& system) noexcept
    :diagnostics(diagnostic_ports),reports(hardware_ports,report_ports,diagnostics),hardware(hardware_ports,i2c,diagnostics,reports),
     controller(hardware,hardware_ports,diagnostics,reports),leds(hardware_ports,diagnostics),application(controller,leds,hardware_ports,diagnostics,*this,wifi_status,backend_status),
     storage(storage_ports,diagnostics),wifi_events(application),wifi(wifi_status,wifi_ports,hardware_ports,diagnostics,wifi_events),
     backend(backend_status,application.data,backend_client,runtime,diagnostics),updater(update_client,update_ports,runtime,diagnostics),
     device(application,controller,hardware,wifi,wifi_status,backend_status,*this,diagnostics,reports.csv.data()),http(listener,runtime,diagnostics,device),
     storage_ports_(storage_ports),hardware_ports_(hardware_ports),runtime_(runtime),listener_(listener),report_ports_(report_ports),system_(system) {
    application.data.initialize("0.0.35","www.picobrew.com",diagnostics);reports.bind(hardware,controller);
    wifi.ap_ssid=application.data.ap_ssid.data();wifi.website=application.data.website.data();
}
void Machine::setup() {
    report_ports_.console("");diagnostics.message("***********************************************************************");diagnostics.message("*                              PicoStill                              *");diagnostics.message("***********************************************************************");
    system_.inspect_partitions();system_.initialize_hardware(hardware);storage.initialize();storage.load(application.stored);report_eeprom();system_.initialize_wifi();
    const auto mac=system_.efuse_mac();char text[16];snprintf(text,sizeof(text),"%02x%02x%02x%02x%02x%02x",static_cast<unsigned>(mac&255),static_cast<unsigned>((mac>>8)&255),static_cast<unsigned>((mac>>16)&255),static_cast<unsigned>((mac>>24)&255),static_cast<unsigned>((mac>>32)&255),static_cast<unsigned>((mac>>40)&255));application.data.set_uid(text,diagnostics);
    leds.setup();hardware_ports_.pin_mode(13,1);
    backend.extra_headers.fill(0);const std::string headers=std::string("Host: ")+application.data.website.data()+"\nConnection: close\n\n";std::memcpy(backend.extra_headers.data(),headers.data(),std::min<std::size_t>(headers.size(),63));
    application.data.set_ssid(application.stored.ssid.data(),diagnostics);application.data.set_password(application.stored.password.data(),diagnostics);report_info();
}
void Machine::poll(ServicePoll service) {
    switch(service){
    case ServicePoll::Update:updater.poll();application.updater_state=updater.state;break;
    case ServicePoll::ConsoleReceiver:receive_console();break;
    case ServicePoll::Wifi:wifi.poll();break;
    case ServicePoll::Backend:backend.poll();break;
    case ServicePoll::Http:http.poll();break;
    case ServicePoll::ConsoleCommands:dispatch_console();break;
    }
}
void Machine::integer(const char* scope,const char* name,std::uint32_t value){runtime_.integer(scope,name,value);}
void Machine::listen_http(unsigned port){listener_.listen(port);}
void Machine::start_firmware_update() {
    hardware_ports_.variable("MAIN","COMMAND","updateStart");
    if(!backend.update_address[0]){diagnostics.message("No update address");return;}
    diagnostics.require(updater.state==0,"HttpUpdate.h",0x10f,"_state == State::Idle");
    std::array<char,129> address={{0}};std::memcpy(address.data(),backend.update_address.data(),128);update_parameters.parse(address.data(),diagnostics);update_parameters.report(runtime_);
    diagnostics.require(update_parameters.valid,"HttpUpdate.h",0x112,"_params.AreValid");
    // Invalid/null URL pointer behavior is intentionally rejected by the owned parser.
    if(!update_parameters.valid)return;
    updater.begin(update_parameters.host.data(),update_parameters.path.data());application.updater_state=updater.state;
}
void Machine::sleep(unsigned seconds){storage.save(application.stored);system_.deep_sleep(static_cast<std::uint32_t>(seconds*1000000u));}
void Machine::factory_reset() {
    hardware_ports_.variable("MAIN","COMMAND","factoryReset");hardware_ports_.variable("SYS","CMD","esp_ota_set_boot_partition");system_.erase_nvs();system_.select_factory_partition();application.stored=EepromData{};sleep(2);
}
void Machine::report_eeprom(){runtime_.integer("EPRM","Version",application.stored.version);system_.credential_variable("EPRM","Ssid",application.stored.ssid.data(),false);system_.credential_variable("EPRM","Password",application.stored.password.data(),true);}
void Machine::report_info() {
    // OEM metadata is explicitly labeled as reference metadata in this source;
    // it is not the date/commit of this reconstruction.
    hardware_ports_.variable("MAIN","BUILD_DATE","Dec  3 2019");hardware_ports_.variable("MAIN","BUILD_TIME","14:19:55");hardware_ports_.variable("MAIN","STILL_VERSION","0.0.35");hardware_ports_.variable("MAIN","GIT_BRANCH","master");hardware_ports_.variable("MAIN","GIT_HASH","25d9c6b2b9c7f7b11ccfcc1fca9d71667a8d0e42");
    hardware_ports_.variable("MAIN","VERSION",application.data.version.data());hardware_ports_.variable("MAIN","WEBSITE",application.data.website.data());hardware_ports_.variable("MAIN","UID",application.data.uid.data());hardware_ports_.variable("MAIN","APSSID",application.data.ap_ssid.data());
    system_.credential_variable("MAIN","SSID",application.ssid.data(),false);system_.credential_variable("MAIN","PASS",application.password.data(),true);hardware_ports_.variable("MAIN","TOKEN",application.data.token.data());hardware_ports_.variable("MAIN","STATE",to_string(application.state));hardware_ports_.variable("MAIN","ERROR",to_string(application.error));system_.report_partitions();
}
void Machine::append_command(const char* command) {
    if(!command || !*command)return;
    const auto length=std::strlen(command);diagnostics.check(length<queued_remaining,"datalen < _left Buffer not big enough");const auto copied=std::min<std::size_t>(length,queued_remaining);std::memcpy(queued.data()+queued_length,command,copied);queued_length+=copied;queued_remaining-=copied;
}
void Machine::queue_console(const char* command){if(queued_length==0)append_command(command);else diagnostics.message("Command discarded");}
void Machine::receive_console() {
    while(queued_length==0){
        if(system_.serial_available()==0)return;
        const char value=static_cast<char>(system_.serial_read());
        if(value==0 || value=='\r' || value=='\n'){queue_console(receiving.data());receiving.fill(0);receiving_length=0;receiving_remaining=127;return;}
        diagnostics.check(receiving_remaining!=0,"_left > 0 Buffer not big enough");
        if(receiving_remaining){receiving[receiving_length++]=value;--receiving_remaining;}
        if(receiving_remaining==0){queue_console(receiving.data());receiving.fill(0);receiving_length=0;receiving_remaining=127;return;}
    }
}
void Machine::test_leds(){Leds test(hardware_ports_,diagnostics);test.setup();for(unsigned index=1;index<39;++index){const auto* program=animation_program(static_cast<Animation>(index));test.start(program->text,2);while(test.length)test.poll();}}
void Machine::dispatch_console() {
    if(queued_length==0)return;
    std::array<char,128> command={{0}};bounded_copy(command.data(),command.size(),queued.data(),diagnostics);queued.fill(0);queued_length=0;queued_remaining=127;const char* text=command.data();
    if(string_equal(text,"info"))report_info();
    else if(string_equal(text,"test"))self_tests();
    else if(string_starts_with(text,"led "))leds.start(text+4,3);
    else if(string_equal(text,"load"))storage.load(application.stored);
    else if(string_equal(text,"save"))storage.save(application.stored);
    else if(string_equal(text,"eeprom"))report_eeprom();
    else if(string_equal(text,"reset"))factory_reset();
    else if(string_equal(text,"exit"))sleep(0);
    else if(string_equal(text,"sleep2"))sleep(2);
    else if(string_equal(text,"restart")){hardware_ports_.variable("MAIN","COMMAND","restart");restart();}
    else if(string_starts_with(text,"setver ")){bounded_copy(application.data.version.data(),16,text+7,diagnostics);hardware_ports_.variable("MAIN","VERSION",application.data.version.data());}
    else if(string_starts_with(text,"setweb ")){bounded_copy(application.data.website.data(),32,text+7,diagnostics);hardware_ports_.variable("MAIN","WEBSITE",application.data.website.data());}
    else if(string_starts_with(text,"setuid ")){bounded_copy(application.data.uid.data(),16,text+7,diagnostics);hardware_ports_.variable("MAIN","UID",application.data.uid.data());}
    else if(string_starts_with(text,"setssid ")){application.data.set_ssid(text+8,diagnostics);bounded_copy(application.stored.ssid.data(),32,application.ssid.data(),diagnostics);hardware_ports_.variable("MAIN","WSSID",application.ssid.data());}
    else if(string_starts_with(text,"setpass ")){application.data.set_password(text+8,diagnostics);bounded_copy(application.stored.password.data(),64,application.password.data(),diagnostics);hardware_ports_.variable("MAIN","WPASS",application.password.data());}
    else if(string_starts_with(text,"seterror ")){const long long n=::strtoll(text+9,nullptr,10);const auto value=static_cast<std::int32_t>(std::max<long long>(std::numeric_limits<std::int32_t>::min(),std::min<long long>(std::numeric_limits<std::int32_t>::max(),n)));latch_first_error(application.error,static_cast<StillError>(value),diagnostics);}
    else if(string_equal(text,"w ap"))wifi_status.requested=WifiState::AccessPoint;
    else if(string_equal(text,"w connect"))wifi_status.connect(application.ssid.data(),application.password.data(),diagnostics);
    else if(string_equal(text,"w idle"))wifi_status.requested=WifiState::Idle;
    else if(string_equal(text,"syncfirmware"))request_firmware_address();
    else if(string_equal(text,"syncerror"))send_error(application.error);
    else if(string_equal(text,"ota"))start_firmware_update();
    else if(string_starts_with(text,"s "))hardware.enable_sensors(string_boolean(text+2));
    else if(string_starts_with(text,"f "))hardware.fans.set_power(string_boolean(text+2));
    else if(string_starts_with(text,"p "))hardware.pump.set_pump(string_boolean(text+2));
    else if(string_equal(text,"lp"))application.queue_button(true,1);
    else if(string_equal(text,"sp"))application.queue_button(false,1);
    else if(string_equal(text,"spp"))application.queue_button(false,2);
    else if(string_equal(text,"test v"))controller.start(ProgramId::PumpTest);
    else if(string_equal(text,"test c"))controller.start(ProgramId::HardwareTest);
    else if(string_equal(text,"test p"))controller.start(ProgramId::PinTest);
    else if(string_equal(text,"test f"))controller.start(ProgramId::FullTest);
    else if(string_equal(text,"test leds"))test_leds();
    else if(string_equal(text,"hw snap"))reports.snapshot_report(hardware.capture());
}
}} // namespace picostill::oem
