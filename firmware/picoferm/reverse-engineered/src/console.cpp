#include "picoferm/application.hpp"
#include <cstring>
namespace picoferm { namespace oem {
bool Application::queue_console(const char* command){if(console_ready||!command)return false;bounded_copy(console.data(),console.size(),command,ports_);console_length=std::strlen(console.data());console_ready=true;return true;}
void Application::dispatch_console(){
    if(!console_ready)return;
    char command[128];bounded_copy(command,sizeof(command),console.data(),ports_);console.fill(0);console_length=0;console_ready=false;
    const auto equals=[&](const char* value){return std::strcmp(command,value)==0;};
    const auto starts=[&](const char* value){return std::strncmp(command,value,std::strlen(value))==0;};
    if(equals("info")){allow_listen_timeout=false;info();}
    else if(equals("requestrestart")){restart_requested=true;ports_.boolean("MAIN","_restartRequested",true);}
    else if(equals("battery"))ports_.boolean("MAIN","HAS_BATTERY",battery_voltage(hardware_)>battery_threshold);
    else if(equals("voltage"))ports_.number("MAIN","VOLT",battery_voltage(hardware_));
    else if(equals("load"))load_storage();
    else if(equals("save"))ports_.persist(measurements.data.bytes.data(),PersistentData::size);
    else if(equals("reset"))factory_reset();
    else if(equals("cal"))calibrate();
    else if(equals("test"))run_embedded_tests();
    else if(equals("adc"))acquisition.start();
    else if(equals("adccont"))acquisition.continuous=!acquisition.continuous;
    else if(equals("sync"))backend.sync(static_cast<std::int32_t>(sleep_seconds/60),battery_voltage(hardware_),session_state==SessionState::Fermenting);
    else if(equals("otau"))update();
    else if(equals("exit")){persist();sleep(0);}
    else if(equals("sleep2")){persist();sleep(2);}
    else if(equals("restart"))restart();
    else if(starts("led "))leds.set_text(command+4);
    else if(equals("run"))run_requested=true;
    else if(equals("setfermenting")){session_state=SessionState::Fermenting;ports_.text("MAIN","SESSION_STATE","Fermenting");}
    else if(equals("setidle")){session_state=SessionState::Idle;ports_.text("MAIN","SESSION_STATE","Idle");}
    else if(equals("sethaswifi")){measurements.data.bytes[0x61]=1;ports_.boolean("MAIN","HAS_WIFI",true);}
    else if(starts("setver ")){bounded_copy(data.version.data(),data.version.size(),command+7,ports_);ports_.text("MAIN","VERSION",data.version.data());}
    else if(starts("setweb ")){bounded_copy(data.website.data(),data.website.size(),command+7,ports_);backend.set_headers(data.website.data());ports_.text("MAIN","WEBSITE",data.website.data());}
    else if(starts("setuid ")){bounded_copy(data.uid.data(),data.uid.size(),command+7,ports_);ports_.text("MAIN","UID",data.uid.data());}
    else if(starts("setssid ")){bounded_copy(reinterpret_cast<char*>(measurements.data.bytes.data()),32,command+8,ports_);data.update_token(ssid(),password(),ports_);ports_.text("MAIN","WSSID",ssid());}
    else if(starts("setpass ")){bounded_copy(reinterpret_cast<char*>(measurements.data.bytes.data()+32),64,command+8,ports_);data.update_token(ssid(),password(),ports_);ports_.text("MAIN","WPASS",password());}
    else if(equals("w ap"))wifi.request(WifiState::AccessPoint);
    else if(equals("w connect"))wifi.connect(ssid(),password());
    else if(equals("w idle"))wifi.request(WifiState::Idle);
    else if(equals("fakedata")){for(int temperature=70;temperature<82;++temperature)measurements.data.append({static_cast<float>(temperature),static_cast<float>(temperature-30)/10.0f},ports_);}
    else if(equals("faketimeout"))button.woke_board=false;
}
}}
