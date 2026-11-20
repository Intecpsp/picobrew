#pragma once
#include "zseries/core_flow.hpp"
#include <algorithm>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace core_flow_fixture {
struct Port {
    std::uint32_t now, address, network;
    std::vector<std::uint32_t> events;
    void event(std::uint32_t op,std::uint32_t a=0,std::uint32_t b=0,std::uint32_t c=0) {events.insert(events.end(),{op,a,b,c});}
};
inline std::uint32_t clock(void* p){return static_cast<Port*>(p)->now;}
inline bool tick(void* p,CoreTickStage stage,std::uint32_t now){static_cast<Port*>(p)->event(100,static_cast<unsigned>(stage),now);return true;}
inline bool perform(void* p,const CoreAction& action,std::uint32_t& result){
    auto& port=*static_cast<Port*>(p);auto a=action.argument,b=action.second_argument,c=action.third_argument;
    if(action.credentials){a=action.credentials->ssid[0]!=0;b=action.credentials->password[0]!=0;c=0;}
    if(action.wifi){a=static_cast<unsigned>(action.wifi->type);b=action.wifi->argument;c=action.wifi->second_argument;}
    if(action.runtime){a=static_cast<unsigned>(action.runtime->type);b=action.runtime->previous;c=action.runtime->current;}
    if(action.still){a=static_cast<unsigned>(action.still->type);b=static_cast<unsigned>(action.still->state);c=0;}
    if(action.command){a=static_cast<unsigned>(action.command->id);b=c=0;}
    if(action.effect==CoreEffect::DispatchTextCommand){a=reinterpret_cast<std::uintptr_t>(action.text_command);b=c=0;}
    port.event(static_cast<unsigned>(action.effect),a,b,c);
    result=action.effect==CoreEffect::EthernetAddress?port.address:action.effect==CoreEffect::SetNetwork?port.network:0;
    return true;
}
template<std::size_t N> void text(std::array<char,N>& out,const char* value){out.fill(0);std::strncpy(out.data(),value,N-1);}
const char* sensor_errors[]={"","WORT temp","HEX temp","DRAIN temp","AMBIENT temp","fixture-error"};
inline void load(CoreFlow& core,std::uint32_t* w){
    auto& config=core.configuration;auto& s=core.state;auto& o=core.observed;
    s.id=static_cast<CoreStateId>(w[0]);s.state_date=w[1];s.service_mode=w[2];s.rinse_pending=w[3];s.offline=w[4];
    s.session_type=static_cast<SessionType>(w[5]);s.session_mode=static_cast<SessionMode>(w[6]);s.program_id=w[7];
    s.runtime_start_pending=w[8];s.persist_credentials=w[9];s.ethernet_address_present=w[10];s.download_requested=w[11];
    s.session_stop_requested=w[12];s.azure_enabled=w[13];o.temperature_polling_enabled=w[14];o.service_button_held=w[15];
    o.system_error=w[16];o.msp_id=static_cast<MspStateId>(w[17]);o.msp_errors=w[18];for(unsigned i=0;i<4;++i)o.temperature_dates[i]=w[19+i];
    text(o.sensor_error,sensor_errors[w[23]]);o.runtime_id=static_cast<RuntimeStateId>(w[24]);o.encoder_activity_date=w[25];
    o.http_backend_state=w[26];o.backend_sync_done=w[27];o.backend_error=w[28];o.registered=w[29];o.update_available=w[30];
    o.update_byte_11b58=w[31];o.update_byte_11b68=w[32];o.update_client_state=w[33];o.session_creation_error=w[34];
    o.session_logger_state=w[35];o.use_still_wifi=w[36];config.use_ethernet=w[37];config.azure_enabled=static_cast<BoolOption>(w[38]);
    config.still_session_mode=static_cast<SessionMode>(w[39]);text(config.wifi_ssid,w[40]?"saved-network":"");
    text(s.pending_credentials.ssid,w[41]?"pending-network":"");text(config.wifi_password,w[61]?"saved-password":"");
    text(s.pending_credentials.password,w[62]?"pending-password":"");
    core.standby_delay_ms=w[45];core.boot_delay_ms=w[46];core.ethernet_timeout_ms=w[47];s.assertion_line=w[63];
    CoreWifiEvent wifi;wifi.type=static_cast<WifiEventType>(w[49]);wifi.argument=w[50];wifi.second_argument=w[51];core.wifi_events.value=wifi;
    CoreRuntimeEvent runtime;runtime.type=static_cast<RuntimeEventType>(w[52]);runtime.previous=w[53];runtime.current=w[54];core.runtime_events.value=runtime;
    CoreCommand command;command.id=static_cast<CoreCommandId>(w[55]);core.commands.value=command;
    CoreButtonEvent button;button.id=w[56];core.button_events.value=button;
    core.text_commands.value=reinterpret_cast<const void*>(static_cast<std::uintptr_t>(w[57]));
    CoreStillEvent still;still.type=static_cast<StillEventId>(w[58]);still.state=static_cast<PicoStillStateId>(w[59]);core.still_events.value=still;
    core.wifi_events.pending=w[48]&1;core.runtime_events.pending=w[48]&2;core.commands.pending=w[48]&4;
    core.button_events.pending=w[48]&8;core.text_commands.pending=w[48]&16;core.still_events.pending=w[48]&32;
}
inline void save(CoreFlow& core,std::uint32_t* w,char* credentials){
    auto& config=core.configuration;auto& s=core.state;auto& o=core.observed;
    w[0]=static_cast<unsigned>(s.id);w[1]=s.state_date;w[2]=s.service_mode;w[3]=s.rinse_pending;w[4]=s.offline;
    w[5]=static_cast<unsigned>(s.session_type);w[6]=static_cast<unsigned>(s.session_mode);w[7]=s.program_id;
    w[8]=s.runtime_start_pending;w[9]=s.persist_credentials;w[10]=s.ethernet_address_present;w[11]=s.download_requested;
    w[12]=s.session_stop_requested;w[13]=s.azure_enabled;w[14]=o.temperature_polling_enabled;w[15]=o.service_button_held;
    w[16]=o.system_error;w[17]=static_cast<unsigned>(o.msp_id);w[18]=o.msp_errors;for(unsigned i=0;i<4;++i)w[19+i]=o.temperature_dates[i];
    for(unsigned i=0;i<6;++i)if(std::strcmp(o.sensor_error.data(),sensor_errors[i])==0)w[23]=i;
    w[24]=static_cast<unsigned>(o.runtime_id);w[25]=o.encoder_activity_date;w[26]=o.http_backend_state;w[27]=o.backend_sync_done;
    w[28]=o.backend_error;w[29]=o.registered;w[30]=o.update_available;w[31]=o.update_byte_11b58;w[32]=o.update_byte_11b68;
    w[33]=o.update_client_state;w[34]=o.session_creation_error;w[35]=o.session_logger_state;w[36]=o.use_still_wifi;
    w[37]=config.use_ethernet;w[38]=static_cast<unsigned>(config.azure_enabled);w[39]=static_cast<unsigned>(config.still_session_mode);
    w[40]=config.wifi_ssid[0]!=0;w[41]=s.pending_credentials.ssid[0]!=0;
    w[48]=core.wifi_events.pending|(core.runtime_events.pending<<1)|(core.commands.pending<<2)|(core.button_events.pending<<3)|(core.text_commands.pending<<4)|(core.still_events.pending<<5);
    w[55]=static_cast<unsigned>(core.commands.value.id);w[61]=config.wifi_password[0]!=0;w[62]=s.pending_credentials.password[0]!=0;w[63]=s.assertion_line;
    const char* texts[]={config.wifi_ssid.data(),config.wifi_password.data(),s.pending_credentials.ssid.data(),s.pending_credentials.password.data()};
    std::memset(credentials,0,4*65);for(unsigned i=0;i<4;++i)std::strncpy(credentials+i*65,texts[i],64);
}
}
