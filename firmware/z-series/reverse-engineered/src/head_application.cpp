#include "zseries/head_application.hpp"
#include "zseries/head_remote_session.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace zseries { namespace oem {
namespace {
template<std::size_t N,std::size_t M>void copy_text(std::array<char,N>& out,const std::array<char,M>& in){out.fill(0);std::memcpy(out.data(),in.data(),std::min(N-1,M));}
HeadApplication& owner(void* context){return *static_cast<HeadApplication*>(context);}
}
const std::array<HeadConsoleDescriptor,6>& head_console_commands()noexcept{
    static const std::array<HeadConsoleDescriptor,6> commands{{
        {HeadConsoleCommand::Splash,"splash","Show splash screen"},
        {HeadConsoleCommand::Hardware,"screen hw","Show HW state screen"},
        {HeadConsoleCommand::Menu,"menu test","Show test menu"},
        {HeadConsoleCommand::UiMenu,"menu uitest","Show ui test menu"},
        {HeadConsoleCommand::Tests,"tests","Run all tests()"},
        {HeadConsoleCommand::Slideshows,"test ss","Shows all slideshow screens"}}};return commands;
}
HeadApplication::HeadApplication()noexcept:ui(numeric.state,text.state,flow.transient_timer),network(numeric.state.parameters),screens(display,encoder.state,periodic.state){
    display_text_font(display_text_context,*display_font(DisplayFontId::Font6x8),0);screens.text_context=&display_text_context;
    flow.services.context=this;flow.services.millis=[](void* p){auto& h=owner(p);return h.services.millis(h.services.context);};
    flow.services.pending=[](void* p,HeadEventKind kind){return owner(p).pending(kind);};
    flow.services.handle_event=[](void* p,HeadEventKind kind){return owner(p).consume(kind);};
    flow.services.perform=[](void* p,const HeadAction& action){return owner(p).flow_action(action);};
    core.services.context=this;core.services.millis=flow.services.millis;
    core.services.perform=[](void* p,const HeadCoreAction& action,std::uint32_t& result){return owner(p).core_action(action,result);};
    core.services.assertion=[](void* p,std::uint32_t line){return owner(p).assertion(line);};
    ui.services.context=this;ui.services.millis=flow.services.millis;
    ui.services.perform=[](void* p,const HeadUiAction& action){return owner(p).ui_action(action);};
    ui.services.assertion=[](void* p,std::uint32_t line){return owner(p).assertion(line);};
    encoder.services.context=this;encoder.services.millis=flow.services.millis;
    encoder.services.perform=[](void* p,HeadEncoderEffect effect,std::uint32_t first,std::uint32_t second){auto& h=owner(p);return h.screen_result(h.screens.encoder_effect(effect,first,second));};
    encoder.services.assertion=[](void* p,std::uint32_t line){return owner(p).assertion(line);};
    periodic.services.context=this;
    periodic.services.perform=[](void* p,UiScreenTickEffect effect,std::uint32_t first,std::uint32_t second){auto& h=owner(p);return h.screen_result(h.screens.tick_effect(effect,first,second));};
    events.services.context=this;events.services.perform=[](void* p,HeadEventEffect effect,std::uint32_t value){return owner(p).event_effect(effect,value);};
    numeric.services.context=this;numeric.services.perform=[](void* p,HeadNumericEffect effect,const CoreCommand* command){return owner(p).numeric_effect(effect,command);};
    numeric.services.assertion=[](void* p,std::uint32_t line){return owner(p).assertion(line);};
    text.services.context=this;text.services.perform=[](void* p,HeadTextEffect effect,const HeadTextCommand* command){return owner(p).text_effect(effect,command);};
    network.services.context=this;network.services.perform=[](void* p,const HeadNetworkAction& action){return owner(p).network_action(action);};
    network.services.assertion=[](void* p,std::uint32_t line){return owner(p).assertion(line);};
    screens.services.context=this;screens.services.millis=flow.services.millis;
    screens.services.assertion=[](void* p,std::uint32_t line){return owner(p).assertion(line);};
    screens.services.post_event=[](void* p,const HeadScreenEvent& event){owner(p).receive_screen(event);return true;};
    screens.slideshow.services.scope=[](void* p,bool begin,const char* tag,const char* file,std::uint32_t line,const char* name){auto& h=owner(static_cast<HeadScreens*>(p)->services.context);return h.services.scope&&h.services.scope(h.services.context,begin,tag,file,line,name);};
}
bool HeadApplication::assertion(std::uint32_t line){
    switch(line){
    case 0x37:return assertion("Timer.h",line,"duration > 0");
    case 0x5e:return assertion("Graphics.h",line,"lines.count > 0");
    case 0x7d:return assertion("Quantity.h",line,"areCompatible(_unit, unit)");
    case 0x7f:return assertion("Head.h",line,"_screen.data.getItemsCount() > 0");
    case 0x97:return assertion("Head.h",line,"_screen.data.getItemsCount() > 0");
    case 0xf8:return assertion("Graphics.h",line,"height <= rect.getHeight()");
    case 0x102:return assertion("Head.h",line,"currentProgress <= 100");
    case 0x103:return assertion("Graphics.h",line,"rect.getWidth() > textRect.getWidth()");
    case 0x124:return assertion("IUiManagement.h",line,"ItemsCount > 0");
    case 0x126:return assertion("IUiManagement.h",line,"idx < ItemsCount");
    case 0x1a0:return assertion("IPicoStill.h",line,"hasError()");
    case 0x1a6:return assertion("ICore.h",line,"hasIndex()");
    case 0x1a8:return assertion("ICore.h",line,"hasRecipeRef()");
    case 0x1ed:return assertion("UiManagement.h",line,"!CString::isNullOrEmpty(title)");
    case 0x1f0:return assertion("UiManagement.h",line,"availableRect.getHeight() == ScreenRect.getHeight() && _defaultCommand == UiCommand::None");
    case 0x236:return assertion("UiManagement.h",line,"maxItemsOnScreen > 0");
    case 0x2ba:return assertion("Head.h",line,"ev.id == QuantityId::StepTemperature || ev.id == QuantityId::StepDuration || ev.id == QuantityId::StepDrain");
    case 0x2c2:return assertion("UiManagement.h",line,"delta != 0");
    case 0x2e7:return assertion("UiManagement.h",line,"percentComplete <= 100");
    case 0x308:return assertion("UiManagement.h",line,"data.getItemsCount() > 0");
    case 0x32b:return assertion("UiManagement.h",line,"info.BodyTextInfoPtr->Count == 3");
    case 0x349:return assertion("Head.h",line,"ev.id == QuantityId::CoffeeBatchSize || ev.id == QuantityId::CoffeeIntensity || ev.id == QuantityId::CoffeeTemperature || ev.id == QuantityId::CoffeeDuration");
    case 0x36a:return assertion("UiComponents.h",line,"idx >= 0 && idx < _charsCount");
    case 0x370:return assertion("UiManagement.h",line,"info.BodyTextInfoPtr != nullptr");
    case 0x39e:return assertion("UiComponents.h",line,"numLimit <= 14");
    case 0x3bf:return assertion("Display.h",line,"text && len > 0 && strlen(text) > 0");
    case 0x406:return assertion("UiComponents.h",line,"_numLimit == RFID_CHARS_COUNT");
    case 0x46a:return assertion("UiComponents.h",line,"_current < _charsCount");
    case 0x481:return assertion("UiComponents.h",line,"next >= 0");
    case 0x4ba:return assertion("UiComponents.h",line,"fMin <= fCur && fCur <= fMax");
    case 0x4d4:return assertion("UiComponents.h",line,"!_isActive");
    case 0x5e0:return assertion("UiComponents.h",line,"id < QuantityId::None");
    case 0x614:return assertion("UiComponents.h",line,"isEnumValid(loc)");
    case 0x79f:return assertion("UiComponents.h",line,"level > 0 && level < 10");
    case 0x86f:return assertion("UiComponents.h",line,"_rect.getHeight() > 0");
    case 0x879:return assertion("UiComponents.h",line,"_rect.getHeight() >= (visibleHeight - _messageBoxMargin*2)");
    case 0x8b4:return assertion("Head.h",line,"_params.isCoffeeProgram()");
    case 0x8c6:return assertion("Head.h",line,"ev.Data.hasRecipeRef()");
    case 0x8e0:return assertion("UiComponents.h",line,"visibleHeight <= _rect.getHeight()");
    case 0x9b3:return assertion("UiComponents.h",line,"!empty");
    default:status.result=HeadApplicationResult::InvalidState;status.detail=line;return false;
    }
}
bool HeadApplication::assertion(const char* source,std::uint32_t line,const char* expression){
    if(!services.assertion){status.result=HeadApplicationResult::MissingBinding;status.detail=line;return false;}
    if(services.assertion(services.context,source,line,expression))return true;
    status.result=HeadApplicationResult::ServiceFailure;status.detail=line;return false;
}
bool HeadApplication::screen_result(HeadScreenResult value){if(value==HeadScreenResult::Ok)return true;status.result=HeadApplicationResult::RenderFailure;status.detail=static_cast<std::uint32_t>(value);return false;}
bool HeadApplication::post(const CoreCommand& command,const HeadUiCommandData* data){
    if(!services.post_core){status.result=HeadApplicationResult::MissingBinding;return false;}
    const HeadUiCoreEnvelope envelope(command,data);return services.post_core(services.context,envelope);
}
bool HeadApplication::post(CoreCommandId id){CoreCommand command;command.id=id;return post(command);}
void HeadApplication::refresh_observed()noexcept{
    if(settings){screens.language=ui.language=core.language=network.language=settings->language;screens.observed.imperial=settings->use_imperial!=0;}
    screens.slideshow.services.diagnostics=services.diagnostics;
    screens.observed.show_detail=settings&&settings->show_detail;screens.observed.static_cycle=settings&&settings->slideshow_cycle_static;
    screens.display_io=services.display_io;screens.display_delay=services.display_delay;
    static_cast<HeadDynamicObserved&>(screens.observed)=observed;
    screens.observed.imperial=settings&&settings->use_imperial!=0;
    const auto& runtime=observed.runtime;
    flow.observed.core=observed.core.id;flow.observed.runtime=runtime.id;flow.observed.instruction=runtime.instruction.opcode;
    flow.observed.downloading=core.state.update_progress_visible;flow.observed.downloaded_bytes=observed.backend.download_received;flow.observed.download_bytes=observed.backend.download_total;
    events.state.runtime=runtime.id;events.state.instruction=runtime.instruction.opcode;events.state.pause=static_cast<PauseReason>(runtime.pause_reason);events.state.screen=static_cast<ScreenId>(runtime.screen);
    core.observed=observed.backend;core.observed.runtime=runtime.id;
    ui.observed.core=observed.core.id;ui.observed.session=observed.core.session_type;ui.observed.service_mode=observed.core.service_mode;ui.observed.t1_monitor=observed.t1_monitor;ui.observed.runtime_location=runtime.manual.location;
    ui.observed.session_name=observed.session.name.data();ui.observed.session_name_length=static_cast<std::uint32_t>(std::strlen(observed.session.name.data()));
    network.core=observed.core.id;
    screens.observed.service_mode=observed.core.service_mode;screens.observed.offline=observed.core.offline;
    screens.observed.temperature=observed.runtime_inputs.heater_target;screens.observed.minimum_temperature=runtime.input_temperature_min;screens.observed.maximum_temperature=runtime.input_temperature_max;
    screens.observed.duration=runtime.time_seconds;screens.observed.session=observed.core.session_type;
}
HeadApplicationResult HeadApplication::setup(){
    status=HeadApplicationStatus{};
    if(!settings||!services.millis||!services.register_commands){status.result=HeadApplicationResult::MissingBinding;return status.result;}
    refresh_observed();
    if(display_setup(display,services.display_io,services.display_setup)!=DisplayResult::Ok||!services.register_commands(services.context,*this,head_console_commands())){status.result=HeadApplicationResult::SetupFailure;return status.result;}
    initialized=true;return HeadApplicationResult::Ok;
}
HeadApplicationResult HeadApplication::tick(std::uint32_t now){
    status=HeadApplicationStatus{};
    if(!settings||!services.millis){status.result=HeadApplicationResult::MissingBinding;return status.result;}
    refresh_observed();const auto result=flow.tick(now);
    if(result!=HeadFlowResult::Ok&&status.result==HeadApplicationResult::Ok){status.result=HeadApplicationResult::ServiceFailure;status.detail=static_cast<std::uint32_t>(result);}
    return status.result;
}
HeadApplicationResult HeadApplication::synchronize(){
    if(!settings){status.result=HeadApplicationResult::MissingBinding;return status.result;}
    refresh_observed();if(flow.synchronize()!=HeadFlowResult::Ok&&status.result==HeadApplicationResult::Ok)status.result=HeadApplicationResult::ServiceFailure;
    return status.result;
}
HeadApplicationResult HeadApplication::display_power(bool enabled){
    if(display_command(display,services.display_io,enabled?0xaf:0xae)!=DisplayResult::Ok){status.result=HeadApplicationResult::ServiceFailure;
            return status.result;}return HeadApplicationResult::Ok;
}
bool HeadApplication::receive_screen(const HeadScreenEvent& event)noexcept{
    switch(event.kind){
    case HeadScreenEventKind::Ui:return ui_events.put(event.ui);
    case HeadScreenEventKind::Temperature:return temperature_events.put(event.temperature);
    case HeadScreenEventKind::Duration:return duration_events.put(event.value);
    case HeadScreenEventKind::Strength:return strength_events.put(static_cast<std::int32_t>(event.value));
    case HeadScreenEventKind::Location:return location_events.put(event.value);
    case HeadScreenEventKind::Quantity:{HeadQuantityEvent value;value.id=event.quantity_id;value.value=event.quantity;return quantity_events.put(value);}
    case HeadScreenEventKind::Text:{HeadTextEvent value;value.command=event.ui.command;value.text=event.text;return text_events.put(value);}
    }return false;
}
bool HeadApplication::pending(HeadEventKind kind)const noexcept{
    switch(kind){
    case HeadEventKind::Core:return core_events.pending;case HeadEventKind::Button:return button_events.pending;
    case HeadEventKind::Encoder:return encoder_events.pending;case HeadEventKind::TextInput:return text_events.pending;
    case HeadEventKind::Temperature:return temperature_events.pending;case HeadEventKind::Time:return duration_events.pending;
    case HeadEventKind::Abv:return strength_events.pending;case HeadEventKind::Valve:return location_events.pending;
    case HeadEventKind::Quantity:return quantity_events.pending;case HeadEventKind::Still:return still_events.pending;
    case HeadEventKind::Wifi:return wifi_events.pending;case HeadEventKind::Runtime:return runtime_events.pending;
    case HeadEventKind::UiCommand:return ui_events.pending;case HeadEventKind::Azure:return azure_events.pending;
    }return false;
}
bool HeadApplication::consume(HeadEventKind kind){
    status.event=kind;refresh_observed();
    switch(kind){
    case HeadEventKind::Core:{core.state.transient=flow.transient_timer;const auto result=core.consume(core_events);flow.transient_timer=core.state.transient;screens.painter.dirty=screens.painter.dirty||core.state.display_dirty;core.state.display_dirty=false;flow.observed.downloading=core.state.update_progress_visible;return result==HeadCoreResult::Ok;}
    case HeadEventKind::Button:{CoreButtonEvent value;button_events.take(value);const auto result=events.button_event(value.type!=0);ui.state.display_suspended=events.state.encoder_press_hijacked;return result==HeadEventResult::Ok;}
    case HeadEventKind::Encoder:{std::int32_t value=0;encoder_events.take(value);encoder.state.transient_timer=flow.transient_timer;const auto result=encoder.rotate(value);flow.transient_timer=encoder.state.transient_timer;screens.painter.dirty=screens.painter.dirty||encoder.state.display_dirty;encoder.state.display_dirty=false;return result==HeadEncoderResult::Ok;}
    case HeadEventKind::TextInput:{HeadTextEvent value;text_events.take(value);return text.input(value.command,value.text)==HeadTextResult::Ok;}
    case HeadEventKind::Temperature:{float value=0;temperature_events.take(value);return numeric.temperature(value)==HeadNumericResult::Ok;}
    case HeadEventKind::Time:{std::uint32_t value=0;duration_events.take(value);return numeric.duration(value)==HeadNumericResult::Ok;}
    case HeadEventKind::Abv:{std::int32_t value=0;strength_events.take(value);return numeric.strength(value)==HeadNumericResult::Ok;}
    case HeadEventKind::Valve:{std::uint32_t value=0;location_events.take(value);return numeric.location(value)==HeadNumericResult::Ok;}
    case HeadEventKind::Quantity:{HeadQuantityEvent value;quantity_events.take(value);return numeric.quantity(value.id,value.value)==HeadNumericResult::Ok;}
    case HeadEventKind::Still:consume_head_still_event(still_events);return true;
    case HeadEventKind::Wifi:{CoreWifiEvent value;wifi_events.take(value);return network.wifi(value)==HeadNetworkResult::Ok;}
    case HeadEventKind::Runtime:{CoreRuntimeEvent value;runtime_events.take(value);HeadRuntimeEvent event;event.type=value.type;event.notification=value.notification;return events.runtime_event(event)==HeadEventResult::Ok;}
    case HeadEventKind::UiCommand:{ui.state.display_suspended=events.state.encoder_press_hijacked;const auto result=ui.consume(ui_events);events.state.encoder_press_hijacked=ui.state.display_suspended;return result==HeadUiResult::Ok;}
    case HeadEventKind::Azure:{HeadAzureEvent value;azure_events.take(value);return network.azure(value)==HeadNetworkResult::Ok;}
    }return false;
}
bool HeadApplication::display_tick(std::uint32_t now){
    animation.dirty=animation.dirty||screens.painter.dirty;screens.painter.dirty=false;
    const auto result=display_animation_tick(display,animation,services.display_io,now);
    if(result!=DisplayEffectResult::Ok){screens.painter.dirty=animation.dirty;return false;}
    return true;
}
bool HeadApplication::flow_action(const HeadAction& action){
    switch(action.effect){
    case HeadEffect::RunningScreen:return running();
    case HeadEffect::Message:return screen_result(screens.waiting(ui_localized_text(UiTextCategory::TextTitle,action.argument,screens.language),nullptr,false));
    case HeadEffect::NetworkRequired:return presentation(HeadUiEffect::NetworkConfiguration);
    case HeadEffect::HomeScreen:return screen_result(screens.home());
    case HeadEffect::SystemAlarm:if(!services.alarm){status.result=HeadApplicationResult::MissingBinding;return false;}return services.alarm(services.context,action.argument!=0);
    case HeadEffect::PostCoreCommand:return post(static_cast<CoreCommandId>(action.argument));
    case HeadEffect::StillScreen:return still_screen();
    case HeadEffect::DownloadProgress:return screen_result(screens.progress(action.argument));
    case HeadEffect::ScreensTick:return periodic.tick(action.argument)==UiScreenTickResult::Ok;
    case HeadEffect::DisplayTick:return display_tick(action.argument);
    }return false;
}
bool HeadApplication::event_effect(HeadEventEffect effect,std::uint32_t argument){
    switch(effect){case HeadEventEffect::Alarm:return flow.alarm(argument!=0)==HeadFlowResult::Ok;case HeadEventEffect::RunningScreen:return running();case HeadEventEffect::NotificationScreen:return notification(argument);case HeadEventEffect::ScreenButton:return screen_result(screens.button());case HeadEventEffect::Synchronize:return flow.synchronize()==HeadFlowResult::Ok;}return false;
}
bool HeadApplication::numeric_effect(HeadNumericEffect effect,const CoreCommand* command){
    switch(effect){case HeadNumericEffect::PostCommand:return command&&post(*command);case HeadNumericEffect::Synchronize:return flow.synchronize()==HeadFlowResult::Ok;case HeadNumericEffect::ManualStepScreen:return presentation(HeadUiEffect::ManualStepScreen);case HeadNumericEffect::CoffeeScreen:return presentation(HeadUiEffect::CoffeeTweak,static_cast<std::uint32_t>(numeric.state.parameters.id),1);case HeadNumericEffect::RecipeStrengthScreen:return presentation(HeadUiEffect::RecipeStrengthScreen);}return false;
}
bool HeadApplication::text_effect(HeadTextEffect effect,const HeadTextCommand* value){
    if(effect==HeadTextEffect::Synchronize)return flow.synchronize()==HeadFlowResult::Ok;
    if(!value)return false;
    CoreCommand command;command.id=value->id;HeadUiCommandData data;
    if(value->credentials){command.credentials=*value->credentials;return post(command);}
    if(value->kit_code){data.has_search=true;data.flags=16;std::memcpy(data.search.data(),value->kit_code,std::min<std::size_t>(16,std::strlen(value->kit_code)));return post(command,&data);}
    data.flags=1;data.index=value->recipe_reference;return post(command,&data);
}
bool HeadApplication::core_action(const HeadCoreAction& action,std::uint32_t& result){
    if(action.effect==HeadCoreEffect::DispatchCoreCommand)return flow.dispatch(static_cast<CoreCommandId>(action.first))==HeadFlowResult::Ok;
    if(action.effect==HeadCoreEffect::CheckRinse){if(!services.check_rinse){status.result=HeadApplicationResult::MissingBinding;return false;}return services.check_rinse(services.context,action.first,result);}
    const auto rendered=screens.core_action(action);if(rendered!=HeadScreenResult::NotHandled)return screen_result(rendered);
    return core_presentation(action);
}
bool HeadApplication::ui_action(const HeadUiAction& action){
    switch(action.effect){
    case HeadUiEffect::Alarm:return flow.alarm(action.first!=0)==HeadFlowResult::Ok;
    case HeadUiEffect::DispatchCoreCommand:return flow.dispatch(static_cast<CoreCommandId>(action.first))==HeadFlowResult::Ok;
    case HeadUiEffect::PostCoreCommand:return action.core_command&&post(*action.core_command,action.data);
    case HeadUiEffect::Synchronize:return flow.synchronize()==HeadFlowResult::Ok;
    case HeadUiEffect::Back:if(events.state.notification!=RuntimeNotificationType::None)events.state.notification=RuntimeNotificationType::None;else text.state.awaiting_wifi_password=false;return flow.synchronize()==HeadFlowResult::Ok;
    case HeadUiEffect::WifiKey:{
        if(!action.event||!services.scan_network){status.result=HeadApplicationResult::MissingBinding;return false;}
        if(!(action.event->flags&1)&&!assertion("ICore.h",0x1a6,"hasIndex()"))return false;
        HeadWifiNetwork network;if(!services.scan_network(services.context,action.event->index&255,network))return false;
        text.state.credentials.ssid=network.ssid;text.state.credentials.password.fill(0);
        if(!network.encrypted){CoreCommand command;command.id=CoreCommandId::WiFiConnect;command.credentials=text.state.credentials;return post(command);}
        text.state.awaiting_wifi_password=true;HeadScreenRequest request;request.screen.layout=8;request.screen.input_kind=2;request.title=ui_localized_text(UiTextCategory::TextTitle,28,screens.language);return screen_result(screens.show(request));
    }
    case HeadUiEffect::ConnectWifi:{if(!settings)return false;CoreCommand command;command.id=CoreCommandId::WiFiConnect;copy_text(command.credentials.ssid,settings->wifi_ssid);copy_text(command.credentials.password,settings->wifi_password);return post(command)&&flow.synchronize()==HeadFlowResult::Ok;}
    case HeadUiEffect::SetImperial:case HeadUiEffect::SetStaticCycle:case HeadUiEffect::SetLanguage:case HeadUiEffect::SetSlideshowDetail:case HeadUiEffect::SetStillLogging:case HeadUiEffect::SetAzureMode:{
        if(!settings||!services.save_configuration){status.result=HeadApplicationResult::MissingBinding;return false;}
        if(action.effect==HeadUiEffect::SetImperial)settings->use_imperial=static_cast<std::uint8_t>(action.first);
        else if(action.effect==HeadUiEffect::SetStaticCycle)settings->slideshow_cycle_static=static_cast<std::uint8_t>(action.first);
        else if(action.effect==HeadUiEffect::SetLanguage)settings->language=static_cast<LanguageType>(action.first);
        else if(action.effect==HeadUiEffect::SetSlideshowDetail)settings->show_detail=static_cast<std::uint8_t>(action.first);
        else if(action.effect==HeadUiEffect::SetStillLogging)settings->still_session_mode=static_cast<SessionMode>(action.first);
        else settings->azure_enabled=static_cast<BoolOption>(action.first);
        if(!services.save_configuration(services.context))return false;
        if(action.effect==HeadUiEffect::SetAzureMode){if(!services.apply_azure_configuration){status.result=HeadApplicationResult::MissingBinding;return false;}if(!services.apply_azure_configuration(services.context))return false;}
        refresh_observed();return flow.synchronize()==HeadFlowResult::Ok;
    }
    default:break;
    }
    if(action.effect==HeadUiEffect::Tests||action.effect==HeadUiEffect::DeveloperTests||(action.effect>=HeadUiEffect::DisplayTest&&action.effect<=HeadUiEffect::EepromScreen))return diagnostic_presentation(action);
    const auto rendered=screens.ui_action(action);if(rendered!=HeadScreenResult::NotHandled)return screen_result(rendered);
    return presentation(action.effect,action.first,action.second);
}
HeadApplicationResult HeadApplication::handle_remote_session(const AzureCommand& incoming,std::uint32_t activity_date){
    status=HeadApplicationStatus{};HeadRemoteServices binding;binding.context=this;
    binding.millis=[](void* p){auto& h=owner(p);return h.services.millis(h.services.context);};
    binding.assertion=[](void* p,const char* file,std::uint32_t line,const char* expression){return owner(p).assertion(file,line,expression);};
    binding.post_azure=[](void* p,const HeadAzureEvent& value){owner(p).receive_azure(value);return true;};
    binding.post_core=[](void* p,const HeadUiCoreEnvelope& value){auto& h=owner(p);return h.post(value.command,value.has_data?&value.data:nullptr);};
    if(services.discard_remote_session)binding.discard=[](void* p){auto& h=owner(p);return h.services.discard_remote_session(h.services.context);};
    if(!services.millis)binding.millis=nullptr;
    const auto result=head_remote_session(incoming,activity_date,remote_input_quiet_ms,binding);
    if(status.result==HeadApplicationResult::Ok&&result!=HeadRemoteResult::Ok)status.result=result==HeadRemoteResult::MissingService?HeadApplicationResult::MissingBinding:HeadApplicationResult::ServiceFailure;
    return status.result;
}

}} // namespace zseries::oem
