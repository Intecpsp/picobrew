#include "zseries/core_application.hpp"
#include "zseries/msp_images.hpp"
#include "zseries/network_probe.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
using namespace zseries::oem;

struct Fixture {
    CoreEventMailbox head;
    CoreApplication app{head};
    std::uint32_t now=2000,assertions=0;
    unsigned control_depth=0,control_entries=0,scope_count=0;
    std::vector<std::uint32_t> lines;
    std::string serial,executed;
    std::size_t serial_position=0;
    Fixture(){
        app.ports.context=this;app.ports.millis=[](void* p){return static_cast<Fixture*>(p)->now;};
        auto& d=app.ports.diagnostics;d.context=this;
        d.log=[](void*,ConsoleLevel,const char*,const char*,std::uint32_t,const char*){return true;};
        d.assertion=[](void* p,const char*,std::uint32_t line,const char*){
            auto& f=*static_cast<Fixture*>(p);++f.assertions;f.lines.push_back(line);f.now+=200;return true;
        };
        app.ports.backend_scope.context=this;
        app.ports.backend_scope.emit=[](void* p,const char* tag,const char* file,std::uint32_t line,const char* name){
            auto& f=*static_cast<Fixture*>(p);assert(!std::strcmp(tag,"REG")&&!std::strcmp(file,"CoreHelpers.h")&&line==0xca&&!std::strcmp(name,"stop"));
            ++f.scope_count;return true;
        };
        app.ports.dispatch_text=[](void* p,const char* text){
            auto& f=*static_cast<Fixture*>(p);f.executed=text;
            // Nested publication must not invalidate the text being consumed.
            assert(f.app.post_text("next"));assert(f.executed==text);return true;
        };
        app.network=2;
    }
    void effect(CoreEffect id,std::uint32_t first=0,std::uint32_t second=0){
        CoreAction action(id);action.argument=first;action.second_argument=second;std::uint32_t ignored=0;
        assert(app.perform(action,ignored));
    }
    void dispatch(){CoreCommand command;assert(app.core.commands.take(command));
        CoreAction action(CoreEffect::DispatchCoreCommand);action.command=&command;std::uint32_t value=0;
        assert(app.perform(action,value));}
};

void registration(){
    Fixture f;auto& a=f.app;
    assert(a.core.transition(CoreStateId::Registering)==CoreFlowResult::Ok);
    assert(a.registration.enabled&&a.registration.deadline==12000&&a.backend.state==0);
    // A refresh command does not change an already running helper's deadline.
    f.effect(CoreEffect::RegistrationRequest,0);assert(a.registration.deadline==12000);
    a.backend_state.ready=false;assert(a.tick_stage(CoreTickStage::Registration,12001));assert(a.backend.state==0);
    a.backend_state.ready=true;assert(a.tick_stage(CoreTickStage::Registration,12000));assert(a.backend.state==0);
    assert(a.tick_stage(CoreTickStage::Registration,12001));assert(a.backend.state==1&&a.registration.pending);
    CoreEvent transition;assert(f.head.take(transition));assert(transition.type==CoreEventType::StateTransition);
    a.backend.state=0;a.backend_state.ready=true;a.backend_state.token_expired=true;
    assert(a.tick_stage(CoreTickStage::Registration,12002));assert(!a.registration.enabled&&!a.registration.pending);
    assert(f.scope_count==1);
    CoreEvent expired;assert(f.head.take(expired));assert(expired.type==CoreEventType::RegistrationTokenExpired);
    f.effect(CoreEffect::RegistrationRequest,0);assert(a.registration.enabled&&a.registration.deadline==0);
}

void ownership(){
    Fixture f;auto& a=f.app;a.core.state.id=CoreStateId::Idle;
    CoreCommand command;command.id=CoreCommandId::ZKitSmartListGet;
    HeadUiCommandData data;data.index=40;data.has_search=true;std::strcpy(data.search.data(),"first");
    HeadUiCoreEnvelope value(command,&data);assert(a.post(value));
    std::strcpy(value.data.search.data(),"changed");std::strcpy(data.search.data(),"changed again");
    CoreCommand dropped;dropped.id=CoreCommandId::PowerButton;assert(a.post(dropped));
    f.dispatch();assert(a.backend.offset==40&&std::strcmp(a.backend.search,"first")==0&&a.backend.state==19);
    std::strcpy(data.search.data(),"second");assert(a.post(HeadUiCoreEnvelope(command,&data)));f.dispatch();
    assert(std::strcmp(a.backend.search,"first")==0); // Busy requests retain borrowed search data.
    a.backend.state=0;assert(a.post(HeadUiCoreEnvelope(command,&data)));f.dispatch();
    assert(std::strcmp(a.backend.search,"second")==0);
    CoreCommand invalid;invalid.detail=&data;assert(!a.post(invalid));
    assert(&a.backend.mailbox==&f.head&&&a.publication.mailbox==&f.head);
    CoreEvent earlier;earlier.reset(CoreEventType::PowerButton);f.head.pending=false;assert(f.head.put(earlier));
    a.publication.ready();CoreEvent retained;assert(f.head.take(retained));assert(retained.type==CoreEventType::PowerButton);
    a.ports.remote_session=[](void* context,const AzureCommand& command,std::uint32_t){
        auto& f=*static_cast<Fixture*>(context);f.executed=command.session.name.data();return true;
    };
    AzureCommand remote;remote.id=CoreCommandId::AzureSessionLoad;
    std::strcpy(remote.session.name.data(),"owned Azure session");assert(a.post_azure(remote));
    std::strcpy(remote.session.name.data(),"mutated");assert(a.post_azure(remote));
    f.dispatch();assert(f.executed=="owned Azure session");
}

void sessions(){
    Fixture f;auto& a=f.app;
    a.session_logger.state=SessionLogState::Creating;a.sessions.state=0;a.sessions.created_session_id=123;
    a.commands.session.session_id=0;assert(a.tick_stage(CoreTickStage::SessionLogger,f.now));
    assert(a.commands.session.session_id==123&&a.session_logger.retained_session_id==123);
    assert(a.session_logger.state==SessionLogState::Waiting&&f.assertions==0);
    a.session_logger.state=SessionLogState::Creating;a.sessions.created_session_id=0;
    assert(a.tick_stage(CoreTickStage::SessionLogger,f.now));
    assert(a.commands.session.session_id==0&&a.session_logger.error&&a.session_logger.state==SessionLogState::Idle);
    a.backend_state.resumable_session_id=456;a.commands.resumed_session=true;
    f.effect(CoreEffect::BeginSession);assert(!a.commands.resumed_session);
    assert(a.commands.session.session_id==456&&a.session_logger.state==SessionLogState::Waiting);
    a.azure.state=AzureState::SessionLoaded;a.refresh_observed();assert(a.commands.program_source_17b0==3);
    auto& watch=a.runtime.state.t1_drop_watch;watch.maximum=88;watch.triggered=true;watch.threshold=5;
    f.effect(CoreEffect::StartT1Monitor,7,1);assert(watch.enabled&&!watch.triggered&&watch.maximum==0&&watch.threshold==1);
    watch.maximum=72;watch.triggered=true;f.effect(CoreEffect::StartT1Monitor,7,1);
    assert(watch.maximum==72&&watch.triggered);f.effect(CoreEffect::CancelT1Monitor);
    assert(!watch.enabled&&watch.maximum==72&&watch.triggered);
}

void still(){
    Fixture f;auto& a=f.app;std::uint32_t result=0;
    PicoStillAction acquire(PicoStillEffect::AcquireRuntime),release(PicoStillEffect::ReleaseRuntime);
    assert(a.still_effect(acquire,result)&&result==1&&a.still_owns_wifi);
    assert(a.still_effect(acquire,result)&&result==0&&f.assertions==1&&f.lines.back()==0x656);
    std::strcpy(a.eeprom.data.wifi_ssid.data(),"home");std::strcpy(a.eeprom.data.wifi_password.data(),"home-key");
    std::strcpy(a.core.state.pending_credentials.ssid.data(),"previous");
    std::strcpy(a.core.state.pending_credentials.password.data(),"previous-key");
    a.wifi.state.id=WiFiStateId::Idle;a.network=2;a.core.state.ethernet_address_present=true;
    assert(a.still_effect(release,result)&&result==1&&!a.still_owns_wifi);
    assert(a.network==0&&!a.core.state.ethernet_address_present);
    assert(std::strcmp(a.wifi.state.requested.ssid.data(),"previous")==0);
    assert(std::strcmp(a.core.state.pending_credentials.ssid.data(),"home")==0);
    assert(a.core.state.pending_credentials.byte_80);
    assert(a.still_effect(release,result)&&result==0);
    assert(a.still_effect(acquire,result));a.wifi.state.id=WiFiStateId::Connected;
    std::strcpy(a.wifi.state.connected.ssid.data(),"home");std::strcpy(a.wifi.state.connected.password.data(),"home-key");
    a.wifi.state.connected.internet_expected=true;a.network=2;
    assert(a.still_effect(release,result)&&a.network==2);
}

void serial(){
    Fixture f;auto& a=f.app;f.serial="commands\nnext line\n";
    auto& io=a.ports.serial_reader;io.context=&f;io.diagnostics=a.ports.diagnostics;
    io.available=[](void* p){auto& f=*static_cast<Fixture*>(p);return static_cast<int>(f.serial.size()-f.serial_position);};
    io.read=[](void* p){auto& f=*static_cast<Fixture*>(p);return static_cast<int>(f.serial[f.serial_position++]);};
    io.post_command=[](void* p,const char* text){auto& f=*static_cast<Fixture*>(p);
        return post_text_command(f.app.text_commands,text,"Core::TextCommandEventQ",f.app.ports.diagnostics)==TextCommandPostResult::Accepted;};
    assert(a.tick_stage(CoreTickStage::SerialCommand,f.now));assert(a.core.text_commands.pending);
    const void* command=nullptr;assert(a.core.text_commands.take(command));
    CoreAction action(CoreEffect::DispatchTextCommand);action.text_command=command;std::uint32_t value=0;
    assert(a.perform(action,value));assert(f.executed=="commands");
    assert(a.text_commands.pending&&a.core.text_commands.pending);
    assert(std::strcmp(a.text_commands.text.data(),"next")==0);
}

void images(){
    assert(z_msp_image_crc(&production_msp_image)==0x5fdb);
    assert(z_msp_image_crc(&unit_test_msp_image)==0xc60a);
    assert(production_msp_image.segment_count==4&&unit_test_msp_image.segment_count==5);
    Fixture f;assert(f.app.setup()==ApplicationResult::MissingBinding);
    f.app.power_button.state.pressed=true;f.app.encoder.button.state.pressed=false;
    f.app.refresh_observed();assert(!f.app.core.observed.service_button_held);
    f.app.encoder.button.state.pressed=true;f.app.refresh_observed();assert(f.app.core.observed.service_button_held);
    f.app.ports.millis=nullptr;assert(f.app.tick(0)==ApplicationResult::MissingBinding);
}

void snapshots(){
    Fixture f;auto& a=f.app;
    a.ports.control.context=&f;
    a.ports.control.enter=[](void* p){auto& f=*static_cast<Fixture*>(p);assert(f.control_depth==0);++f.control_depth;++f.control_entries;};
    a.ports.control.leave=[](void* p){auto& f=*static_cast<Fixture*>(p);assert(f.control_depth==1);--f.control_depth;};
    a.heater.synchronization=a.ports.control;
    HeaterIo pins{&f,nullptr,[](void* p,std::uint8_t,bool){assert(static_cast<Fixture*>(p)->control_depth==1);}};
    assert(heater_enable(a.heater,pins,true));assert(f.control_depth==0);
    assert(heater_set_target(a.heater,80,99));assert(f.control_depth==0);
    a.refresh_observed();assert(a.runtime.observed.heater_target==80&&f.control_depth==0);
    std::string response;
    struct Output {Fixture* fixture;std::string* text;} output{&f,&response};
    a.local_server.services.transport.context=&output;
    a.local_server.services.transport.write=[](void* p,const std::uint8_t* bytes,std::size_t size){
        auto& out=*static_cast<Output*>(p);assert(out.fixture->control_depth==0);
        out.text->append(reinterpret_cast<const char*>(bytes),size);return true;
    };
    assert(a.local_server.services.response.state(a.local_server.services.response.context));
    assert(response.find("HTTP/1.0 200 OK")!=std::string::npos);
    assert(f.control_entries>=4&&f.control_depth==0);
}

void auxiliary_failures(){
    Fixture f;auto& a=f.app;auto& io=a.ports.sensor_bus;
    io.context=&f;
    io.begin_transmission=[](void*,std::uint8_t){};
    io.write=[](void*,std::uint8_t){return std::size_t(1);};
    io.end_transmission=[](void*,bool){return std::uint8_t(1);};
    io.request_from=[](void*,std::uint8_t,std::size_t,bool){return std::size_t(0);};
    io.read=[](void*){return -1;};
    a.ports.sensor_clock={&f,[](void* p){return static_cast<Fixture*>(p)->now;}};
    assert(a.setup_stage(CoreSetupStage::Accelerometer));
    assert(a.setup_stage(CoreSetupStage::Pressure));
    assert(!a.accelerometer.polling_enabled&&!a.pressure.polling_enabled);
    a.accelerometer.polling_enabled=true;a.accelerometer.raw={{100,-200,300}};
    a.pressure.polling_enabled=true;a.pressure.raw_pressure=415148;a.pressure.raw_temperature=519888;
    assert(a.tick_stage(CoreTickStage::Accelerometer,f.now));
    assert(a.auxiliary.acceleration[0]!=0&&a.accelerometer.raw[0]==100);
    assert(a.tick_stage(CoreTickStage::Pressure,f.now));
    assert(a.pressure.raw_pressure==415148&&a.pressure.raw_temperature==519888);
    assert(a.auxiliary.pressure_ms==f.now&&a.auxiliary.pressure_pa==0);
    // A wrong identity diagnoses and returns, while later setup remains usable.
    io.end_transmission=[](void*,bool){return std::uint8_t(0);};
    io.request_from=[](void*,std::uint8_t,std::size_t count,bool){return count;};
    io.read=[](void*){return 0;};
    assert(a.setup_stage(CoreSetupStage::Accelerometer));
    assert(f.assertions==1&&f.lines.back()==0x8f);
    assert(a.setup_stage(CoreSetupStage::Pressure));
    assert(f.assertions==2&&f.lines.back()==0x102&&a.pressure.polling_enabled);
    // A missing binding is still a composition error, not a sensor error.
    io.read=nullptr;assert(!a.tick_stage(CoreTickStage::Pressure,f.now));
}

void adc_failures(){
    Fixture f;auto& a=f.app;
    struct Bus {std::size_t returned=7,index=0;int bytes[8]={8,0,-1,0,8,-1,8,0};} bus;
    a.ports.sensor_bus.context=&bus;
    a.ports.sensor_bus.request_from=[](void* p,std::uint8_t,std::size_t,bool){auto& b=*static_cast<Bus*>(p);b.index=0;return b.returned;};
    a.ports.sensor_bus.read=[](void* p){auto& b=*static_cast<Bus*>(p);return b.bytes[b.index++];};
    a.ports.sensor_clock={&f,[](void* p){return static_cast<Fixture*>(p)->now;}};
    a.acquisition.adc.polling_enabled=true;
    for(auto& sample:a.temperatures.temperatures){sample.celsius=20;sample.updated_ms=99;}
    assert(a.tick_stage(CoreTickStage::Temperatures,f.now));
    for(const auto& sample:a.temperatures.temperatures)assert(sample.celsius==20&&sample.updated_ms==99);
    // Failed reads diagnose but still form the OEM masked values. The valid
    // channels publish after their zero-time assertions; invalid ones retain.
    bus.returned=8;f.now=0;
    assert(a.tick_stage(CoreTickStage::Temperatures,0));
    assert(f.assertions==5&&f.now==1000);
    assert(a.acquisition.adc.raw[1]==3840&&a.acquisition.adc.raw[2]==4095);
    for(unsigned i=0;i<4;++i)assert(a.temperatures.temperatures[i].updated_ms==(i==2?99u:0u));
    assert(a.temperatures.temperatures[2].celsius==20);
}

int main(int argc,char** argv){assert(argc==2);const std::string which=argv[1];
    if(which=="registration")registration();else if(which=="ownership")ownership();else if(which=="sessions")sessions();
    else if(which=="still")still();else if(which=="serial")serial();else if(which=="images")images();
    else if(which=="snapshots")snapshots();else if(which=="auxiliary_failures")auxiliary_failures();
    else if(which=="adc_failures")adc_failures();else assert(false);
}
