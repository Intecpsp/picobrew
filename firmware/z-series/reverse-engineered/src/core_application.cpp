#include "zseries/core_application.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
namespace {
CoreApplication& app(void* context) { return *static_cast<CoreApplication*>(context); }
std::uint32_t clock(void* context) { auto& self=app(context); return self.ports.millis(self.ports.context); }
template<class Result> bool succeeded(Result value) { return static_cast<unsigned>(value)==0; }
struct ControlGuard {
    const HeaterSynchronization& control;
    explicit ControlGuard(const HeaterSynchronization& value):control(value){if(control.enter&&control.leave)control.enter(control.context);}
    ~ControlGuard(){if(control.enter&&control.leave)control.leave(control.context);}
};
}

CoreApplication::CoreApplication(CoreEventMailbox& output) noexcept
    : core(eeprom.data),programs(program),commands(core,programs,acquisition,heater,temperatures,auxiliary),
      power_button(core.button_events),head_events(output),publication(event,head_events),
      local_application(local_server,program,publication,temperatures,heater,auxiliary,msp_state,runtime,wifi.state.rssi),
      backend(backend_http,backend_state,program,event,head_events),sessions(session_http),azure(azure_commands),
      still(eeprom.data),still_client(still.state,still_samples),still_binding(still_http,still_client) {
    core.services.context=this; core.services.millis=clock;
    core.services.perform=[](void* context,const CoreAction& action,std::uint32_t& value){return app(context).perform(action,value);};
    core.services.tick=[](void* context,CoreTickStage stage,std::uint32_t now){return app(context).tick_stage(stage,now);};
    runtime.msp=&msp; runtime.services.context=this; runtime.services.millis=clock;
    runtime.services.still_command=[](void* context,std::uint32_t id,std::uint32_t selection){
        PicoStillCommand command;command.id=static_cast<StillCommandId>(id);command.selection=static_cast<std::int32_t>(selection);
        app(context).still.commands.put(command);
    };
    runtime.services.still_ready=[](void* context){return app(context).still.state.id==PicoStillStateId::PendingStart;};
    runtime.services.still_cancel=[](void* context){PicoStillCommand command;command.id=StillCommandId::Stop;app(context).still.commands.put(command);};
    runtime.services.event=[](void* context,RuntimeEventType type,std::uint32_t first,std::uint32_t second){
        post_runtime_event(app(context).core,type,first,second);
    };
    runtime.services.find_program=[](void* context,std::uint32_t id,double& rate){
        return ProgramRepository::runtime_find(&app(context).programs,id,rate);
    };
    runtime.services.requeue_command=[](void* context,const RuntimeCommand& command){app(context).post_runtime(command);};
    HelperServices helpers;helpers.context=this;helpers.millis=clock;
    helpers.perform=[](void* context,const HelperAction& action,std::uint32_t& result){return app(context).helper(action,result);};
    helpers.assertion=[](void* context,std::uint32_t line){
        return report_backend_assertion(app(context).ports.diagnostics,BackendDiagnosticDomain::Helpers,line);};
    session_logger.services=helpers;
    registration.interval_ms=10000;
    backend.hooks.context=this;
    backend.hooks.network_ready=[](void* context,bool& ready){return app(context).network_ready(ready);};
    backend.hooks.diagnostic=[](void* context,std::uint32_t line){auto& self=app(context);return report_http_backend_diagnostic(self.ports.diagnostics,self.backend,line);};
    backend.hooks.assertion=[](void* context,std::uint32_t line){return report_backend_assertion(app(context).ports.diagnostics,BackendDiagnosticDomain::Http,line);};
    sessions.hooks.context=this;sessions.hooks.network_ready=backend.hooks.network_ready;
    sessions.hooks.diagnostic=[](void* context,std::uint32_t line){auto& self=app(context);return report_z_backend_diagnostic(self.ports.diagnostics,self.sessions,line);};
    sessions.hooks.assertion=[](void* context,std::uint32_t line){return report_backend_assertion(app(context).ports.diagnostics,BackendDiagnosticDomain::Session,line);};
    still.services.context=this;
    still.services.perform=[](void* context,const PicoStillAction& action,std::uint32_t& result){return app(context).still_effect(action,result);};
    still_client.services=still_http_services(still_binding);
    csv.heater=&heater.state;csv.temperatures=&temperatures;csv.auxiliary=&auxiliary;
    csv.msp=&msp_state;csv.runtime=&runtime.state;csv.still=&still_samples;
    serial_logger.source.context=this;
    serial_logger.source.header=[](void* context,CsvBuffer& buffer,const SerialLogServices& services){
        return core_csv_header(&app(context).csv,buffer,services);
    };
    serial_logger.source.data=[](void* context,CsvBuffer& buffer,const SerialLogServices& services){
        auto& self=app(context);HeaterState heater;HeaterSensors temperatures;
        {ControlGuard guard(self.ports.control);heater=self.heater.state;temperatures=self.temperatures;}
        auto sources=self.csv;sources.heater=&heater;sources.temperatures=&temperatures;
        return core_csv_data(&sources,buffer,services);
    };
    local_application.bind();
    local_server.services.response.context=this;
    local_server.services.response.headers=[](void* context,std::uint32_t status,std::int32_t length,const char* type){
        auto& self=app(context);return write_local_http_headers(self.local_server.services.transport,status,length,type)==LocalHttpResult::Ok;
    };
    local_server.services.response.write=[](void* context,const std::uint8_t* bytes,std::size_t size){
        const auto& io=app(context).local_server.services.transport;return io.write&&io.write(io.context,bytes,size);
    };
    local_server.services.response.state=[](void* context){
        auto& self=app(context);
        LocalHttpState snapshot;
        {ControlGuard guard(self.ports.control);snapshot=local_http_state(self.temperatures,self.heater,self.msp_state,self.runtime,self.wifi.state.rssi);}
        return send_local_http_state(self.local_server,snapshot)==LocalHttpResult::Ok;
    };
}

bool CoreApplication::fail(std::uint32_t detail) {
    status.result=ApplicationResult::ServiceFailure;status.detail=detail;return false;
}
bool CoreApplication::diagnostic(const char* file,std::uint32_t line,const char* text) {
    return ports.diagnostics.log&&ports.diagnostics.log(ports.diagnostics.context,ConsoleLevel::Warning,"CORE",file,line,text);
}
bool CoreApplication::assertion(const char* file,std::uint32_t line,const char* expression) {
    return ports.diagnostics.assertion&&ports.diagnostics.assertion(ports.diagnostics.context,file,line,expression);
}
bool CoreApplication::post(const HeadUiCoreEnvelope& value) {
    if(core.commands.pending)return true;
    queued_command_=value;queued_has_azure_=false;
    core.commands.put(queued_command_.command);return true;
}
bool CoreApplication::post(const CoreCommand& value) {
    // Callers with detail must use the typed envelope overload.
    if(value.detail)return false;
    return post(HeadUiCoreEnvelope(value,nullptr));
}
bool CoreApplication::post_azure(const AzureCommand& value) {
    if(core.commands.pending)return true;
    queued_azure_=value;queued_has_azure_=true;
    CoreCommand command;command.id=value.id;
    HeadUiCommandData data;data.recipe=value.recipe;data.parameters=value.recipe.parameters;
    queued_command_.assign(command,&data);core.commands.put(queued_command_.command);return true;
}
bool CoreApplication::post_text(const char* value) {
    const auto result=post_text_command(text_commands,value,"Core::TextCommandEventQ",ports.diagnostics);
    if(result!=TextCommandPostResult::Accepted&&result!=TextCommandPostResult::Discarded)return false;
    if(text_commands.pending&&!core.text_commands.pending)core.text_commands.put(text_commands.text.data());
    return true;
}
bool CoreApplication::post_runtime(const RuntimeCommand& value) {runtime_commands.put(value);return true;}
bool CoreApplication::receive_wifi(const WifiAction& action) {
    if(action.effect!=WifiEffect::EmitEvent)return false;
    CoreWifiEvent event;event.type=static_cast<WifiEventType>(action.argument);
    event.argument=action.second;event.second_argument=action.third;core.wifi_events.put(event);return true;
}
bool CoreApplication::network_ready(bool& ready) {
    ready=network!=0;return true;
}

void CoreApplication::refresh_observed() noexcept {
    ControlGuard guard(ports.control);
    auto& observed=core.observed;
    observed.temperature_polling_enabled=acquisition.adc.polling_enabled;
    // Core+0x15994 belongs to the encoder's button (GPIO39), not the power
    // button at Core+0x4890/GPIO32. Holding the knob selects service boot.
    observed.service_button_held=encoder.button.state.pressed;
    observed.system_error=system_error;
    observed.msp_id=static_cast<MspStateId>(msp_state.id);observed.msp_errors=msp_state.errors;
    for(std::size_t i=0;i<temperatures.temperatures.size();++i)observed.temperature_dates[i]=temperatures.temperatures[i].updated_ms;
    observed.runtime_id=runtime.state.id;observed.encoder_activity_date=encoder.activity_date;
    observed.http_backend_state=backend.state;observed.backend_sync_done=backend_state.ready;
    observed.backend_error=backend_state.error;observed.registered=backend_state.registered;
    observed.update_available=backend_state.needs_update();
    observed.update_byte_11b58=firmware_download.completed;observed.update_byte_11b68=firmware_download.error;
    observed.update_client_state=static_cast<std::uint32_t>(firmware_http.state);
    observed.session_creation_error=session_logger.error;observed.session_logger_state=static_cast<std::uint32_t>(session_logger.state);
    observed.use_still_wifi=still_owns_wifi;
    refresh_runtime_controls(runtime,heater,temperatures,safeties);
    auto& r=runtime.observed;
    r.system_error=system_error;r.msp_error=msp_state.id!=Z_MSP_APP_RUNNING||msp_state.errors!=0;
    r.pico_still_error=still_has_error(still.state);
    const auto& current=still_samples.current;const auto& ten=still_samples.seconds10;const auto& sixty=still_samples.seconds60;
    r.still_sample_present=current.has_data();r.signal_950_defined=current.has_speed_data();
    r.signal_a00_defined=ten.has_speed_data();r.signal_ab0_defined=sixty.has_speed_data();r.byte_980=current.pump_running;
    r.scalar_958=current.temperature[0];r.scalar_960=current.temperature[1];r.still_t3=current.temperature[2];
    r.scalar_978=current.pressure;r.scalar_9d8=current.speed[0];r.scalar_a28=ten.pressure;
    r.scalar_a98=ten.speed[2];r.scalar_aa8=ten.speed[4];r.scalar_ab8=sixty.temperature[0];
    r.scalar_ad8=sixty.pressure;r.scalar_b48=sixty.speed[2];r.scalar_b58=sixty.speed[4];
    backend.boiler=eeprom.data.boiler;backend.environment.pressure_pascal=auxiliary.pressure_pa;
    backend.environment.estimated_boil_celsius=temperatures.estimated_boil_celsius;
    session_logger.observed.backend_state=sessions.state;session_logger.observed.created_id=sessions.created_session_id;
    session_logger.observed.session_id=commands.session.session_id;
    session_logger.observed.resumed_id=static_cast<std::uint32_t>(backend_state.resumable_session_id);
    session_logger.observed.step_name=runtime.state.step_name;
    azure.core_state=core.state.id;
    commands.program_source_17b0=static_cast<std::uint32_t>(azure.state);
}

bool CoreApplication::setup_timer_services(CoreStartupServices& services) {
    if(!ports.timers.start_periodic_software_timer||!ports.timers.begin_hardware_timer||
       !ports.timers.attach_timer_interrupt||!ports.timers.write_timer_alarm||!ports.timers.enable_timer_alarm)return false;
    services={};services.context=this;
    services.setup_component=[](void* context,CoreSetupStage stage){return app(context).setup_stage(stage);};
    services.read_boiler_type=[](void* context,BoilerType& value){value=app(context).eeprom.data.boiler;return true;};
    services.start_periodic_software_timer=[](void* context,std::uint32_t period,std::uintptr_t id,CoreTimerCallback callback,std::uint32_t block,PlatformHandle& handle){
        auto& p=app(context).ports.timers;return p.start_periodic_software_timer(p.context,period,id,callback,block,handle);
    };
    services.begin_hardware_timer=[](void* context,std::uint8_t id,std::uint16_t divider,bool count){auto& p=app(context).ports.timers;return p.begin_hardware_timer(p.context,id,divider,count);};
    services.attach_timer_interrupt=[](void* context,PlatformHandle h,CoreTimerCallback callback,bool edge){auto& p=app(context).ports.timers;p.attach_timer_interrupt(p.context,h,callback,edge);};
    services.write_timer_alarm=[](void* context,PlatformHandle h,std::uint64_t ticks,bool reload){auto& p=app(context).ports.timers;p.write_timer_alarm(p.context,h,ticks,reload);};
    services.enable_timer_alarm=[](void* context,PlatformHandle h,bool enabled){auto& p=app(context).ports.timers;p.enable_timer_alarm(p.context,h,enabled);};
    services.register_heater_commands=[](void* context){auto& self=app(context);
        return self.ports.register_heater_commands&&self.ports.register_heater_commands(self.ports.context);};
    services.assertion=[](void* context,std::uint32_t line,const char* expression){return app(context).assertion("Core.h",line,expression);};
    services.register_adc_commands=[](void* context){auto& self=app(context);
        return self.ports.register_adc_commands&&self.ports.register_adc_commands(self.ports.context);};
    return true;
}
ApplicationResult CoreApplication::setup() {
    status={};CoreStartupServices startup{};
    if(!ports.millis||!ports.setup||!ports.register_heater_commands||!ports.register_adc_commands||!ports.head_tick||!ports.head_wifi||!ports.head_runtime||!ports.head_still||
       !ports.display_power||!ports.azure_window_closed||!ports.remote_session||!ports.check_network||
       !ports.ethernet_connect||!ports.ethernet_address||!ports.restart||!ports.select_factory||!ports.dispatch_text||
       !ports.scan_count||!ports.scan_ssid||!ports.still_authorization||!ports.msp_image||
       ((ports.control.enter==nullptr)!=(ports.control.leave==nullptr))||
       !setup_timer_services(startup)||!bind_runtime_heater(runtime,runtime_heater,heater,temperatures,ports.heater_gpio))
        return status.result=ApplicationResult::MissingBinding;
    heater.synchronization=ports.control;
    runtime.services.diagnostics=ports.diagnostics;
    backend.diagnostics=ports.diagnostics;
    still.services.diagnostics=ports.diagnostics;still_client.services.diagnostics=ports.diagnostics;
    for(auto* client:{&backend_http,&session_http,&firmware_http,&binary_http,&still_http})client->services.diagnostics=ports.diagnostics;
    backend_http.name="Backend";session_http.name="Session";firmware_http.name="Firmware";binary_http.name="Binary";still_http.name="Still";
    backend.token=identifier.data();sessions.token=identifier.data();azure.device_id=identifier.data();
    firmware_download.services.diagnostics=ports.diagnostics;binary_download.services=ports.diagnostics;
    const auto result=setup_core_controls(acquisition,heater,timers,ports.sensor_bus,ports.heater_gpio,startup,&status.failed_setup);
    if(result!=CoreStartupResult::Ok){status.detail=static_cast<std::uint32_t>(result);return status.result=ApplicationResult::SetupFailure;}
    refresh_observed();return status.result;
}
bool CoreApplication::setup_stage(CoreSetupStage stage) {
    status.failed_setup=stage;
    switch(stage) {
    case CoreSetupStage::Wifi:return succeeded(wifi.setup());
    case CoreSetupStage::Buzzer:return succeeded(buzzer.setup());
    case CoreSetupStage::Eeprom:return succeeded(eeprom.setup());
    case CoreSetupStage::PowerButton:return succeeded(power_button.setup());
    case CoreSetupStage::Encoder:return succeeded(encoder.setup());
    case CoreSetupStage::Msp:
        if(z_msp_setup(&msp,ports.msp_image)!=Z_MSP_OK)return fail(1);
        break;
    case CoreSetupStage::Safeties:if(!initialize_safety_input(ports.safety_gpio))return fail(2);break;
    case CoreSetupStage::Accelerometer:
        return (lis3dhtr_setup(accelerometer,ports.sensor_bus,&ports.diagnostics)&(AuxiliaryMissingService|AuxiliaryDiagnosticFailure))==0;
    case CoreSetupStage::Pressure:
        return (bmp280_setup(pressure,ports.sensor_bus,&ports.diagnostics)&(AuxiliaryMissingService|AuxiliaryDiagnosticFailure))==0;
    case CoreSetupStage::PicoStill:return succeeded(still.setup());
    case CoreSetupStage::Azure:if(!succeeded(azure.setup()))return fail(3);break;
    default:break;
    }
    return ports.setup&&ports.setup(ports.context,stage);
}

ApplicationResult CoreApplication::tick(std::uint32_t now) {
    if(!ports.millis)return status.result=ApplicationResult::MissingBinding;
    status={};now_=now;refresh_observed();
    const auto result=core.tick(now);
    // Original Core/SessionLogger share this flag; consuming the error event
    // must clear the real source rather than a temporary observation alone.
    session_logger.error=core.observed.session_creation_error;
    // Core::recoverAzure and the Azure object refer to the same OEM flag.
    // In particular, the disabled branch changes this flag without calling
    // Azure::transition. Preserve that branch across the portable snapshots.
    azure.enabled=core.state.azure_enabled;
    if(result!=CoreFlowResult::Ok&&status.result==ApplicationResult::Ok){status.result=ApplicationResult::ServiceFailure;status.detail=static_cast<std::uint32_t>(result);}
    return status.result;
}
bool CoreApplication::tick_stage(CoreTickStage stage,std::uint32_t now) {
    status.failed_tick=stage;bool result=false;refresh_observed();
    switch(stage) {
    case CoreTickStage::Msp:result=z_msp_poll(&msp,now)==Z_MSP_OK;break;
    case CoreTickStage::Temperatures:{
        HeaterSensors snapshot;{ControlGuard guard(ports.control);snapshot=temperatures;}
        const auto acquired=acquire_temperatures(acquisition,snapshot,now,ports.sensor_bus,ports.sensor_clock,&ports.diagnostics);
        result=acquired==AcquisitionResult::Ok||acquired==AcquisitionResult::ShortReply||
            acquired==AcquisitionResult::ReadFailure||acquired==AcquisitionResult::InvalidClock;
        // Sensor bus transactions occur outside the critical section. Publish
        // only ADC-owned fields; preserve ISR-updated rate/history fields.
        {ControlGuard guard(ports.control);temperatures.temperatures=snapshot.temperatures;}
        break;
    }
    case CoreTickStage::Accelerometer:result=(lis3dhtr_tick(accelerometer,auxiliary,ports.sensor_bus)&AuxiliaryMissingService)==0;break;
    case CoreTickStage::Pressure:{
        HeaterSensors snapshot;{ControlGuard guard(ports.control);snapshot=temperatures;}
        result=(bmp280_tick(pressure,auxiliary,snapshot,ports.sensor_bus,ports.sensor_clock,&ports.diagnostics)&(AuxiliaryMissingService|AuxiliaryDiagnosticFailure))==0;
        {ControlGuard guard(ports.control);temperatures.estimated_boil_celsius=snapshot.estimated_boil_celsius;}
        break;
    }
    case CoreTickStage::PicoStill:result=succeeded(still.tick(now));break;
    case CoreTickStage::System:result=succeeded(tick_system(timing,now,ports.system_tick));break;
    case CoreTickStage::Safeties:{ControlGuard guard(ports.control);
        result=update_safeties(safety_controller,safeties,temperatures,msp_state,now,ports.safety_gpio);break;}
    case CoreTickStage::PowerButton:result=succeeded(power_button.tick(now));break;
    case CoreTickStage::FirmwareUpdate:result=succeeded(firmware_http.tick(now))&&succeeded(firmware_download.callback_result);break;
    case CoreTickStage::BinaryDownload:result=succeeded(binary_http.tick(now))&&succeeded(binary_download.callback_result);break;
    case CoreTickStage::SerialCommand:
        result=succeeded(tick_serial_reader(serial_reader,ports.serial_reader));
        if(text_commands.pending&&!core.text_commands.pending)core.text_commands.put(text_commands.text.data());
        break;
    case CoreTickStage::Wifi:result=succeeded(wifi.tick(now));break;
    case CoreTickStage::HttpServer:result=succeeded(local_server.tick(now));break;
    case CoreTickStage::ZBackend:result=succeeded(sessions.tick(now));break;
    case CoreTickStage::Encoder:result=succeeded(encoder.tick(now));break;
    case CoreTickStage::Head:result=ports.head_tick&&ports.head_tick(ports.context,now);break;
    case CoreTickStage::Runtime:{RuntimeCommand command;runtime_commands.take(command);result=succeeded(runtime.tick(now,command));break;}
    case CoreTickStage::HttpBackend:result=succeeded(backend.tick(now));break;
    case CoreTickStage::SerialLogger:result=succeeded(serial_logger.tick(now));break;
    case CoreTickStage::Registration:{
        RegistrationObserved observed;observed.backend_ready=backend_state.ready;
        observed.registered=backend_state.registered;observed.expired=backend_state.token_expired;
        result=succeeded(tick_registration(registration,observed,now,session_logger.services));break;
    }
    case CoreTickStage::ProgramDownload:{
        program_download.requested=core.state.download_requested;
        ProgramDownloadObserved observed;observed.backend_ready=backend_state.ready;
        observed.complete=binary_download.completed;observed.error=binary_download.error;observed.uri=backend_state.program_uri.text.data();
        result=succeeded(tick_program_download(program_download,observed,session_logger.services));
        core.state.download_requested=program_download.requested;break;
    }
    case CoreTickStage::SessionLogger:
        session_logger.stop_requested=core.state.session_stop_requested;
        // The helper's retained id and Core's session id are aliases in the
        // original object. Apply the assignment before its consistency check,
        // including the failed-create path that stores zero.
        if(session_logger.state==SessionLogState::Creating&&sessions.state==0){
            commands.session.session_id=sessions.created_session_id;
            session_logger.observed.session_id=commands.session.session_id;
        }
        result=succeeded(session_logger.tick(now));core.state.session_stop_requested=session_logger.stop_requested;break;
    case CoreTickStage::Azure:{
        result=succeeded(azure.tick(now));core.state.azure_enabled=azure.enabled;AzureCommand value;
        if(azure_commands.take(value))post_azure(value);
        break;
    }
    }
    refresh_observed();return result||fail(static_cast<unsigned>(stage));
}

SessionSnapshot CoreApplication::session_snapshot() const noexcept {
    SessionSnapshot value;value.type=core.state.session_type;value.session_id=commands.session.session_id;
    value.recipe_id=commands.session.unique_program_id;value.program=static_cast<ZProgramId>(core.state.program_id);
    value.name=commands.session.name.data();value.stats=commands.session.stats;value.parameters=commands.session.parameters;
    value.group_session=commands.session.use_azure;value.recipe_guid=backend.local_recipe.guid.text.data();
    value.still_uid=still.state.selected.data();value.still_version=still.state.version.data();return value;
}
SessionLogSnapshot CoreApplication::session_log_snapshot() const noexcept {
    ControlGuard guard(ports.control);
    const auto local=local_http_state(temperatures,heater,msp_state,runtime,wifi.state.rssi);
    SessionLogSnapshot value;value.session_id=commands.session.session_id;value.thermoblock=local.thermoblock_temperature;
    value.wort=local.wort_temperature;value.ambient=local.ambient_temperature;value.drain=local.drain_temperature;
    value.target=local.target_temperature;value.step_name=local.step_name;value.valve_position=local.valve_position;
    value.keg_pump=(msp_state.pumps&0x10)!=0;value.drain_pump=(msp_state.pumps&0x20)!=0;
    value.pause_reason=local.pause_reason;value.seconds_remaining=local.remaining_seconds;value.rssi=local.rssi;return value;
}
bool CoreApplication::emit(CoreEventType type,std::uint32_t first,std::uint32_t second) {
    event.reset(type);event.argument=first;event.second_argument=second;
    if(type==CoreEventType::BoilerDetectResult){event.detection_error=static_cast<HexDetectError>(first);event.boiler=static_cast<BoilerType>(second);}
    if(type==CoreEventType::CoreError)event.error=static_cast<CoreErrorId>(first);
    head_events.put(event);return true;
}
bool CoreApplication::start_http_download(HttpClient& client,const char* uri,const HttpReceiver& receiver) {
    bool accepted=false;if(!succeeded(client.configure(uri,receiver,HttpClientClass::None,accepted)))return false;
    if(!accepted)return true;
    return succeeded(client.get(accepted));
}
bool CoreApplication::backend_request(const CoreCommand& command) {
    BackendCommand request;request.id=command.id;request.parameters=command.program_parameters;
    if(command.detail){const auto& data=*static_cast<const HeadUiCommandData*>(command.detail);
        request.offset=static_cast<std::int32_t>(data.index);request.reference_id=static_cast<std::int32_t>(data.index);
        request.recipe=data.recipe;request.parameters=data.parameters;
        // A rejected busy request must not overwrite the backing storage of
        // the search pointer retained by the request already in progress.
        if(backend.state==0&&network!=0)backend_search_=data.search;
        request.search=data.has_search?backend_search_.data():nullptr;
    }else if(backend.state==0&&network!=0)backend_search_.fill(0);
    return succeeded(backend.request(request));
}
bool CoreApplication::azure_load() {
    if(!active_has_azure_||!ports.remote_session)return false;
    const bool result=ports.remote_session(ports.context,active_azure_,encoder.activity_date);
    commands.session.use_azure=false;return result;
}
bool CoreApplication::perform(const CoreAction& action,std::uint32_t& result) {
    status.failed_effect=action.effect;result=0;
    switch(action.effect) {
    case CoreEffect::SetNetwork:{
        if(action.argument==0){network=0;result=1;return true;}
        if(network==action.argument&&!assertion("System.h",0x143,"_state.Network != type"))return false;
        bool connected=false;if(!ports.check_network||!ports.check_network(ports.context,action.argument,connected))return false;
        network=connected?action.argument:0;result=connected;return true;
    }
    case CoreEffect::WifiTarget:wifi.select_target(static_cast<WiFiStateId>(action.argument));return true;
    case CoreEffect::WifiConnect:{
        if(!action.credentials)return false;
        WifiCredentials value;value.ssid=action.credentials->ssid;value.password=action.credentials->password;
        value.internet_expected=action.credentials->byte_80;wifi.connect(value);return true;
    }
    case CoreEffect::StartSync:return succeeded(backend.registration());
    case CoreEffect::HeaterTimers:return set_core_heater_timers(timers,action.argument!=0,ports.timers);
    case CoreEffect::PrepareFirmwareUpdate:firmware_download.completed=false;firmware_download.error=false;return true;
    case CoreEffect::StartFirmwareUpdate:return start_http_download(firmware_http,backend_state.update_address.text.data(),firmware_download.receiver());
    case CoreEffect::RegistrationRequest:
        // 0x400d5a88 starts the periodic helper only when it is stopped.
        // The argument chooses delayed vs immediate polling; zero does not
        // disable registration or issue a synchronous backend request.
        if(!registration.enabled){
            registration.enabled=true;
            registration.deadline=action.argument?clock(this)+registration.interval_ms:0;
        }
        return true;
    case CoreEffect::RuntimeStop:{RuntimeCommand value;value.id=RuntimeCommandId::Stop;return post_runtime(value);}
    case CoreEffect::RuntimePause:{RuntimeCommand value;value.id=RuntimeCommandId::Pause;return post_runtime(value);}
    case CoreEffect::RuntimeStart:{RuntimeCommand value;value.id=RuntimeCommandId::Start;value.program_id=action.argument;return post_runtime(value);}
    case CoreEffect::DisplayPower:return ports.display_power&&ports.display_power(ports.context,action.argument!=0);
    case CoreEffect::BeginSession:
        session_logger.resume_pending=commands.resumed_session;refresh_observed();
        if(!succeeded(session_logger.begin()))return false;
        if(commands.resumed_session&&!session_logger.resume_pending)
            commands.session.session_id=session_logger.retained_session_id;
        commands.resumed_session=session_logger.resume_pending;core.state.session_stop_requested=session_logger.stop_requested;return true;
    case CoreEffect::EmitCoreEvent:return emit(static_cast<CoreEventType>(action.argument),action.second_argument,action.third_argument);
    case CoreEffect::EthernetConnect:return ports.ethernet_connect&&ports.ethernet_connect(ports.context,action.argument);
    case CoreEffect::EthernetAddress:if(!ports.ethernet_address)return false;result=ports.ethernet_address(ports.context);return true;
    case CoreEffect::SaveConfiguration:return succeeded(eeprom.write());
    case CoreEffect::AzureRecover:return succeeded(azure.recover(action.argument==0));
    case CoreEffect::StartProgram:return succeeded(commands.start_program(action.argument));
    case CoreEffect::HeadWifiEvent:return action.wifi&&ports.head_wifi&&ports.head_wifi(ports.context,*action.wifi);
    case CoreEffect::StillWifiEvent:if(!action.wifi)return false;still.wifi_events.put(*action.wifi);return true;
    case CoreEffect::HttpServerEnable:return succeeded(local_server.enable(action.argument!=0));
    case CoreEffect::HeadRuntimeEvent:return action.runtime&&ports.head_runtime&&ports.head_runtime(ports.context,*action.runtime);
    case CoreEffect::AzureEndSession:return succeeded(azure.session_end());
    case CoreEffect::DispatchCoreCommand:
        if(!action.command)return false;
        active_command_.assign(*action.command,action.command->detail?static_cast<const HeadUiCommandData*>(action.command->detail):nullptr);
        active_has_azure_=queued_has_azure_&&action.command->id==CoreCommandId::AzureSessionLoad;
        if(active_has_azure_)active_azure_=queued_azure_;
        queued_has_azure_=false;return succeeded(commands.dispatch(active_command_.command));
    case CoreEffect::DispatchTextCommand:
        if(!take_text_command(text_commands,active_text_))return false;
        return ports.dispatch_text&&ports.dispatch_text(ports.context,active_text_.data());
    case CoreEffect::HeadStillEvent:return action.still&&ports.head_still&&ports.head_still(ports.context,*action.still);
    case CoreEffect::Restart:return ports.restart&&ports.restart(ports.context);
    case CoreEffect::RequestBackendCommand:return action.command&&backend_request(*action.command);
    case CoreEffect::AzureDiscard:return succeeded(azure.session_discard());
    case CoreEffect::UiAzureWindowClosed:return ports.azure_window_closed&&ports.azure_window_closed(ports.context,action.argument!=0);
    case CoreEffect::ResetConfiguration:return succeeded(eeprom.reset());
    case CoreEffect::SelectFactoryPartition:return ports.select_factory&&ports.select_factory(ports.context);
    case CoreEffect::AzureSessionLoad:return azure_load();
    case CoreEffect::RuntimePostCommand:return action.runtime_command&&post_runtime(*action.runtime_command);
    case CoreEffect::ClearMspErrors:return z_msp_send(&msp,static_cast<std::uint8_t>(action.argument),0,0)==Z_MSP_OK;
    case CoreEffect::StillCommand:{PicoStillCommand value;value.id=static_cast<StillCommandId>(action.argument);value.selection=static_cast<std::int32_t>(action.second_argument);still.commands.put(value);return true;}
    case CoreEffect::StartT1Monitor:
        // 0x400daf64 ignores its second register argument (7). The next
        // register pair contains the double threshold, 1.0 for this command.
        return succeeded(runtime.start_t1_drop_watch(action.second_argument));
    case CoreEffect::CancelT1Monitor:return succeeded(runtime.reset_t1_drop_watch());
    case CoreEffect::AzureBeginSession:return succeeded(azure.session_begin());
    case CoreEffect::ReadRawTemperatures:{
        const auto result=max11613_poll(acquisition.adc,ports.sensor_bus,&ports.diagnostics);
        return result==AcquisitionResult::Ok||result==AcquisitionResult::ShortReply||result==AcquisitionResult::ReadFailure;
    }
    }
    return false;
}

bool CoreApplication::helper(const HelperAction& action,std::uint32_t& result) {
    result=0;
    switch(action.effect) {
    case HelperEffect::RegistrationRequest:return succeeded(backend.registration());
    case HelperEffect::EmitCoreEvent:return emit(static_cast<CoreEventType>(action.argument));
    case HelperEffect::ConfigureProgramDownload:{
        binary_download.completed=false;binary_download.error=false;bool accepted=false;
        return succeeded(binary_http.configure(action.text,binary_download.receiver(),HttpClientClass::None,accepted));
    }
    case HelperEffect::StartProgramDownload:{bool accepted=false;return succeeded(binary_http.get(accepted));}
    case HelperEffect::CanSynchronize:{bool ready=false;if(!succeeded(sessions.can_sync(ready)))return false;result=ready;return true;}
    case HelperEffect::CreateSession:return succeeded(sessions.create(session_snapshot()));
    case HelperEffect::LogSession:return succeeded(sessions.log(session_log_snapshot()));
    case HelperEffect::EndSession:return succeeded(sessions.end(session_snapshot()));
    case HelperEffect::CancelSessionClient:return succeeded(sessions.cancel());
    case HelperEffect::Diagnostic:return report_core_helper_diagnostic(ports.diagnostics,session_logger,action.argument,ports.backend_scope);
    }
    return false;
}
bool CoreApplication::still_effect(const PicoStillAction& action,std::uint32_t& result) {
    result=0;
    switch(action.effect) {
    case PicoStillEffect::HttpTick:return succeeded(tick_still_http(still_binding,action.argument));
    case PicoStillEffect::ClientTick:{const auto result=still_client.tick();still_client.observe(still.observed);return succeeded(result);}
    case PicoStillEffect::AuthorizationTick:case PicoStillEffect::AuthorizationRequest:
        return ports.still_authorization&&ports.still_authorization(ports.context,action,still.observed);
    case PicoStillEffect::AcquireRuntime:
        if(still_owns_wifi&&!assertion("Core.h",0x656,"!_wiFiIsLocked"))return false;
        if(!still_owns_wifi){still_owns_wifi=true;result=1;}
        return true;
    case PicoStillEffect::ReleaseRuntime:{
        if(!still_owns_wifi)return true;
        still_owns_wifi=false;result=1;
        WifiCredentials home;
        std::copy(eeprom.data.wifi_ssid.begin(),eeprom.data.wifi_ssid.end(),home.ssid.begin());
        std::copy(eeprom.data.wifi_password.begin(),eeprom.data.wifi_password.end(),home.password.begin());
        home.internet_expected=true;
        if(wifi.state.id==WiFiStateId::Connected&&home.equals(wifi.state.connected))return true;
        network=0;core.state.ethernet_address_present=false;
        if(!core.state.pending_credentials.ssid[0]&&!assertion("Core.h",0x66e,"_pendingCreds.hasSsid()"))return false;
        // 0x40101428 requests the previous pending credentials before copying
        // the home credentials into that slot. Preserve that observed order.
        WifiCredentials pending;pending.ssid=core.state.pending_credentials.ssid;
        pending.password=core.state.pending_credentials.password;
        pending.internet_expected=core.state.pending_credentials.byte_80;wifi.connect(pending);
        core.state.pending_credentials.ssid=home.ssid;core.state.pending_credentials.password=home.password;
        core.state.pending_credentials.byte_80=true;return true;
    }
    case PicoStillEffect::WifiTarget:wifi.select_target(static_cast<WiFiStateId>(action.argument));return true;
    case PicoStillEffect::WifiConnect:if(!action.credentials)return false;wifi.connect(*action.credentials);return true;
    case PicoStillEffect::ClientHost:return succeeded(still_client.set_host(action.text));
    case PicoStillEffect::ClientCommand:return succeeded(still_client.command(static_cast<Command>(action.argument)));
    case PicoStillEffect::ClientConfigure:return action.credentials&&succeeded(still_client.configure(*action.credentials));
    case PicoStillEffect::ClientClear:return succeeded(still_client.reset());
    case PicoStillEffect::ScanCount:{std::int32_t count=0;if(!ports.scan_count||!ports.scan_count(ports.context,count))return false;result=static_cast<std::uint32_t>(count);return true;}
    case PicoStillEffect::ScanSsid:return action.ssid&&ports.scan_ssid&&ports.scan_ssid(ports.context,static_cast<std::uint8_t>(action.argument),*action.ssid);
    case PicoStillEffect::EmitEvent:{CoreStillEvent value;value.type=StillEventId::StateTransition;value.state=static_cast<PicoStillStateId>(action.argument);core.still_events.put(value);return true;}
    case PicoStillEffect::RegisterCommands:return ports.setup&&ports.setup(ports.context,CoreSetupStage::PicoStill);
    }
    return false;
}
}} // namespace zseries::oem
