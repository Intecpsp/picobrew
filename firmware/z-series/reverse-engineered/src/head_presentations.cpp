#include "zseries/head_application.hpp"
#include "zseries/head_quantity.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>

namespace zseries { namespace oem {
namespace {
// Original fixed buffers deliberately truncate after formatting.
void format_bounded(char* out,std::size_t size,const char* format,...){va_list arguments;va_start(arguments,format);::vsnprintf(out,size,format,arguments);va_end(arguments);}
using Text=std::array<char,129>;
const char* localized(const HeadScreens& screens,UiTextCategory category,std::uint32_t index){return ui_localized_text(category,index,screens.language);}
const char* title(const HeadScreens& screens,std::uint32_t index){return localized(screens,UiTextCategory::TextTitle,index);}
const char* session_title(const HeadApplication& head,SessionType type){const auto id=static_cast<std::uint32_t>(type);return head.observed.core.id==CoreStateId::Running&&head.observed.session.name[0]&&id-6u<4?head.observed.session.name.data():localized(head.screens,UiTextCategory::SessionType,id);}
bool append(HeadScreens& screens,std::uint32_t command,const char* label=nullptr){return screens.menu_data.append(static_cast<UiCommand>(command),label);}
bool append_index(HeadScreens& screens,std::uint32_t command,std::uint32_t index,const char* label){HeadUiEvent event;event.command=static_cast<UiCommand>(command);event.flags=1;event.index=index;return screens.menu_data.append(event,label);}
HeadScreenResult body(HeadScreens& screens,const char* heading,const char* const* lines,std::uint8_t count,std::uint32_t layout=17,std::uint32_t vertical=2,DisplayTextAlignment horizontal=DisplayTextAlignment::Center){
    HeadScreenRequest request;request.title=heading;request.screen.layout=layout;request.has_body=true;request.body.lines=lines;request.body.count=count;request.body.vertical=1;request.body.horizontal=horizontal;request.menu_vertical=vertical;return screens.show(request);
}
HeadScreenResult wrapped(HeadScreens& screens,const char* heading,const char* value,std::uint32_t layout=17){
    HeadWrappedText lines;if(!head_wrap_text(lines,value))return HeadScreenResult::InvalidText;std::array<const char*,12> pointers{};for(std::uint32_t i=0;i<lines.count;++i)pointers[i]=lines.lines[i].data();return body(screens,heading,pointers.data(),lines.count,layout);
}
Text ip(std::uint32_t address){Text value{};format_bounded(value.data(),value.size(),"%u.%u.%u.%u",address&255,(address>>8)&255,(address>>16)&255,address>>24);return value;}
Text mac(const std::array<std::uint8_t,6>& address){Text value{};format_bounded(value.data(),value.size(),"%02X:%02X:%02X:%02X:%02X:%02X",address[0],address[1],address[2],address[3],address[4],address[5]);return value;}
bool can_drain(const ProgramRuntimeState& state){return !(state.flags&0x1000)&&state.flag_b5&&state.id==RuntimeStateId::Running&&(state.instruction.opcode!=OpCode::WaitUser||state.pause_reason!=3);}
bool valid_strength(std::int32_t value){return static_cast<std::uint32_t>(value)-1u<9;}

bool quantity_label(std::array<char,32>& output,const char* label,const Quantity& quantity,bool metric){std::array<char,16> value{};if(!head_quantity_text(value.data(),value.size(),quantity,metric))return false;format_bounded(output.data(),output.size(),"%s %s",label,value.data());return true;}
}

bool HeadApplication::aligned_line(const char* left,const char* right){
    std::uint32_t l=0,r=0;if(display_utf8_length(left,std::strlen(left)+1,l)!=DisplayTextResult::Ok||display_utf8_length(right,std::strlen(right)+1,r)!=DisplayTextResult::Ok)return false;
    std::string value(left);if(value.size()>22)value.resize(22);const auto padding=21u-l-r;value.append(std::min<std::size_t>(padding,22-value.size()),' ');value.append(right,std::min<std::size_t>(std::strlen(right),22-value.size()));return screens.scroll_data.append(value.c_str());
}
bool HeadApplication::screen(std::uint32_t layout,std::uint32_t heading,UiCommand command){HeadScreenRequest request;request.screen.layout=layout;request.screen.command=command;request.title=heading?title(screens,heading):"";return screen_result(screens.show(request));}

bool HeadApplication::runtime_menu(){
    screens.clear_data();const auto& state=observed.runtime;const auto opcode=static_cast<std::uint32_t>(state.instruction.opcode);const bool running=state.id==RuntimeStateId::Running;
    if(observed.core.azure_enabled)append(screens,67);
    if(running&&opcode!=14)append(screens,55);
    if(!observed.core.offline&&!observed.network&&settings->wifi_ssid[0])append(screens,105);
    if(running&&opcode==14&&(state.pause_reason>17||!(0x21008u&(1u<<state.pause_reason))))append(screens,observed.core.session_type==static_cast<SessionType>(4)?58:56);
    if(state.manual_step_notification&&running&&opcode<19&&(0x5a000u&(1u<<opcode)))append(screens,70);
    if(state.flag_b7&&running&&opcode!=14&&opcode!=52&&state.pid_active)append(screens,68);
    if(state.flag_b6&&running&&opcode==13)append(screens,69);
    if(observed.still.id==PicoStillStateId::Running){if(!observed.still_connectivity_error){append(screens,27);append(screens,24);append(screens,23);}else append(screens,26);}
    if(can_drain(state))append(screens,62);
    if(opcode==13||(opcode>=15&&opcode<=18)||(opcode>=49&&opcode<=53))append(screens,61);
    if(!running||opcode!=14){append(screens,settings->use_imperial?117:116);append(screens,settings->slideshow_cycle_static?122:123);if(runtime_slideshow_visible&&!observed.core.service_mode)append(screens,settings->show_detail?120:119);}
    return append(screens,50);
}
bool HeadApplication::notification(std::uint32_t notification){
    screens.clear_data();if(observed.core.azure_enabled)append(screens,67);append(screens,1);
    HeadScreenRequest request;request.screen.layout=2;request.screen.command=static_cast<UiCommand>(67);request.extra_info=notification;request.title=session_title(*this,observed.core.session_type);return screen_result(screens.show(request));
}
bool HeadApplication::running(){
    if(flow.transient_timer.active)return true;
    if(events.state.notification!=RuntimeNotificationType::None)return notification(static_cast<std::uint32_t>(events.state.notification));
    const auto& runtime=observed.runtime;const auto opcode=static_cast<std::uint32_t>(runtime.instruction.opcode);
    if(runtime.id==RuntimeStateId::Running){
        if(opcode==21){numeric.state.manual=ManualStepParameters{};numeric.state.manual.location=runtime.manual.location;numeric.state.manual.temperature=runtime.manual.temperature;numeric.state.manual.hold=runtime.manual.hold;numeric.state.manual.drain=runtime.manual.drain;numeric.state.manual.byte_00=true;return presentation(HeadUiEffect::ManualStepScreen);}
        if(opcode==22)return screen(7,42);
        if(opcode==19){screens.clear_data();if(observed.core.azure_enabled)append(screens,67);append(screens,65);append(screens,66);append(screens,52);return screen_result(wrapped(screens,session_title(*this,observed.core.session_type),localized(screens,UiTextCategory::TextCommon,17)));}
        if(opcode==23)return screen(9,43);
        if(opcode>=41&&opcode<=55&&(0x6081u&(1u<<(opcode-41))))return still_screen();
    }
    if(runtime.screen)return runtime_instruction();
    if(runtime.id==RuntimeStateId::Running&&opcode==14){if(!runtime_menu())return false;return screen(16);}
    if(runtime.id!=RuntimeStateId::Running&&!assertion("RuntimeState.h",0x4d2,"stateId == RuntimeStateId::Running"))return false;
    const bool info=((opcode-13u>=6)&&(opcode-49u>=7))||(opcode==13&&runtime.instruction.argument<10);
    HeadScreenRequest request;request.screen.layout=info?1:4;request.screen.command=static_cast<UiCommand>(83);request.screen.argument=static_cast<std::uint32_t>(observed.core.session_type);request.title=info?session_title(*this,observed.core.session_type):"";if(!info)runtime_slideshow_visible=true;return screen_result(screens.show(request));
}
bool HeadApplication::still_screen(){
    const auto heading=session_title(*this,observed.core.session_type);screens.clear_data();
    const auto id=static_cast<std::uint32_t>(observed.still.id);
    if(id==17){if(observed.core.azure_enabled)append(screens,67);if(observed.runtime.still.id==7)append(screens,68);append(screens,50);return screen(5,0,static_cast<UiCommand>(83));}
    if(id==3){const char* explanation=nullptr;const bool empty=observed.still.available_count<1;ui_screen_text(static_cast<ScreenId>(empty?75:76),screens.language,explanation);if(empty){append(screens,25);append(screens,52);}else{if(observed.still.available_count>observed.still.available.size())return false;for(std::uint32_t i=0;i<observed.still.available_count;++i)append_index(screens,21,i,observed.still.available[i].data());append(screens,53);}return screen_result(wrapped(screens,heading,explanation));}
    if(id==6){append(screens,22);append(screens,50);const char* explanation=nullptr;ui_screen_text(static_cast<ScreenId>(72),screens.language,explanation);return screen_result(wrapped(screens,heading,explanation));}
    if(id==19){append(screens,52);const char* lines[]={"Error while",to_string(observed.still.previous),localized(screens,UiTextCategory::TextCommon,15)};return screen_result(body(screens,heading,lines,3));}
    append(screens,50);const char* lines[]={localized(screens,UiTextCategory::TextCommon,16),localized(screens,UiTextCategory::TextCommon,14),to_string(observed.still.id)};return screen_result(body(screens,heading,lines,3));
}

bool HeadApplication::presentation(HeadUiEffect effect,std::uint32_t first,std::uint32_t second){
    if(!settings)return false;
    switch(effect){
    case HeadUiEffect::NetworkConfiguration:{
        const char* lines[2]={observed.core.offline?title(screens,41):localized(screens,UiTextCategory::TextCommon,observed.network==2?7:observed.network==1?6:5),observed.wifi.connected.ssid.data()};
        screens.clear_data();if(observed.core.offline){append(screens,94);append(screens,3);}else{if(observed.network==2){append(screens,107);append(screens,106);}else{if(settings->wifi_ssid[0])append(screens,104,settings->wifi_ssid.data());append(screens,106);append(screens,107);}append(screens,109);append(screens,92);if(observed.core.id!=CoreStateId::NeedsNetwork)append(screens,3);}return screen_result(body(screens,title(screens,26),lines,!observed.core.offline&&observed.network==2?2:1));
    }
    case HeadUiEffect::EthernetConfiguration:{screens.clear_data();if(observed.network!=1)append(screens,110);append(screens,3);Text value{};const auto address=ip(observed.ethernet_address),hardware=mac(observed.ethernet_mac);format_bounded(value.data(),64,"%-2s %18s\n%-3s %s","IP",address.data(),"MAC",hardware.data());return screen_result(wrapped(screens,title(screens,17),value.data()));}
    case HeadUiEffect::BoilerConfiguration:{screens.clear_data();append(screens,138);append(screens,1);const char* explanation=nullptr;ui_screen_text(static_cast<ScreenId>(59),screens.language,explanation);Text value{};format_bounded(value.data(),value.size(),"%s\n%s",explanation,to_string(settings->boiler));return screen_result(wrapped(screens,title(screens,6),value.data()));}
    case HeadUiEffect::SlideshowCycleConfiguration:{screens.clear_data();append(screens,settings->slideshow_cycle_static?124:125);append(screens,3);const char* lines[]={localized(screens,UiTextCategory::SlideshowAutoCycle,2),localized(screens,UiTextCategory::SlideshowAutoCycle,3),localized(screens,UiTextCategory::SlideshowAutoCycle,settings->slideshow_cycle_static?0:1)};return screen_result(body(screens,title(screens,47),lines,3));}
    case HeadUiEffect::UnitsConfiguration:{screens.clear_data();append(screens,settings->use_imperial?117:116);append(screens,3);const char* line=localized(screens,UiTextCategory::Units,!settings->use_imperial);return screen_result(body(screens,title(screens,53),&line,1));}
    case HeadUiEffect::AzureConfiguration:{screens.clear_data();const auto mode=static_cast<std::uint32_t>(settings->azure_enabled);if(mode==2)append(screens,131);else if(mode==1)append(screens,130);else{if(mode&&!assertion("Head.h",0x575,"_eeprom.Data.AzureEnabled == BoolOption::Unknown"))return false;append(screens,130);append(screens,131);}append(screens,3);const char* line=localized(screens,UiTextCategory::TextAzure,mode==2?1:mode==1?2:0);return screen_result(body(screens,title(screens,4),&line,1));}
    case HeadUiEffect::StillLoggingConfiguration:{screens.clear_data();const auto mode=static_cast<std::uint32_t>(settings->still_session_mode);if(mode==1)append(screens,128);else if(mode==2)append(screens,127);else{if(mode&&!assertion("Head.h",0x55d,"_eeprom.Data.StillSessionMode == SessionMode::Unknown"))return false;append(screens,127);append(screens,128);}append(screens,3);const char* lines[]={localized(screens,UiTextCategory::TextSessions,0),localized(screens,UiTextCategory::TextSessions,mode==1?1:mode==2?2:0)};return screen_result(body(screens,title(screens,31),lines,mode==0?1:2));}
    case HeadUiEffect::WifiDetails:{
        screens.clear_data();Text line{};const auto address=ip(observed.wifi.local_address),gateway=ip(observed.wifi.gateway_address),hardware=mac(observed.wifi_mac);
        format_bounded(line.data(),32,"%-2s %18s","IP",address.data());screens.scroll_data.append(line.data());format_bounded(line.data(),32,"%-3s %s","MAC",hardware.data());screens.scroll_data.append(line.data());format_bounded(line.data(),32,"%-2s %18s","GW",gateway.data());screens.scroll_data.append(line.data());format_bounded(line.data(),32,"%4s %14ddB","RSSI",static_cast<std::int8_t>(observed.wifi.rssi));screens.scroll_data.append(line.data());format_bounded(line.data(),32,"%-21s","SSID:");screens.scroll_data.append(line.data());format_bounded(line.data(),32,"%s",observed.wifi.connected.ssid.data());screens.scroll_data.append(line.data());format_bounded(line.data(),32,"%-21s","BSSID:");screens.scroll_data.append(line.data());format_bounded(line.data(),32,"%s",observed.wifi_bssid.data());screens.scroll_data.append(line.data());return screen(6,56);
    }
    case HeadUiEffect::CoffeeTweak:{
        if(!second){if(first-20u>=4&&!assertion("Head.h",0x333,"ProgramParams::isCoffeeProgram(programId)"))return false;numeric.state.coffee_defaults=ProgramParameters{};numeric.state.parameters=ProgramParameters{};auto& value=numeric.state.parameters;value.id=static_cast<ZProgramId>(first);value.intensity={first==21?5.0:7.0,Unit::Intensity};value.temperature={195,Unit::Fahrenheit};value.time={420,Unit::Second};value.water=settings->use_imperial?Quantity{4.2,Unit::Gallon}:Quantity{15.5,Unit::Liter};}
        auto& value=numeric.state.parameters;const auto program=static_cast<std::uint32_t>(value.id);if(program-20u>=4){if(!assertion("Head.h",0x2f9,"_params.isCoffeeProgram()"))return false;return true;}
        std::array<std::array<char,32>,4> buffer{};const char* lines[4]{};std::uint8_t count=1;if(!quantity_label(buffer[0],localized(screens,UiTextCategory::TextCommon,25),value.water,!settings->use_imperial))return false;lines[0]=buffer[0].data();
        if(program==21||program==22){if(!quantity_label(buffer[1],localized(screens,UiTextCategory::TextCommon,22),value.intensity,!settings->use_imperial))return false;lines[count++]=buffer[1].data();}
        else if(program==23){if(!quantity_label(buffer[1],localized(screens,UiTextCategory::TextCommon,23),value.temperature,!settings->use_imperial)||!quantity_label(buffer[2],localized(screens,UiTextCategory::TextCommon,24),value.time,!settings->use_imperial))return false;lines[count++]=buffer[1].data();lines[count++]=buffer[2].data();}
        screens.clear_data();const bool supported[]={true,program==21||program==22,program==23,program==23};const Quantity defaults[]={numeric.state.coffee_defaults.water,numeric.state.coffee_defaults.intensity,numeric.state.coffee_defaults.temperature,numeric.state.coffee_defaults.time};
        for(unsigned i=0;i<4;++i)if(supported[i]&&static_cast<std::uint32_t>(defaults[i].unit)>=8)append(screens,36+i);
        HeadUiEvent load;load.command=static_cast<UiCommand>(40);load.flags=8;load.parameters=value;screens.menu_data.append(load,localized(screens,UiTextCategory::UiCommand,static_cast<std::uint32_t>(load.command)));
        for(unsigned i=0;i<4;++i)if(supported[i]&&static_cast<std::uint32_t>(defaults[i].unit)<8)append(screens,36+i);
        append(screens,3);
        return screen_result(body(screens,title(screens,program==20?8:program==21?11:program==22?10:9),lines,count));
    }
    case HeadUiEffect::RecipeStrengthScreen:{
        screens.clear_data();const auto& recipe=ui.state.recipe;const auto& params=numeric.state.parameters;
        if(valid_strength(recipe.parameters.abv)&&static_cast<std::uint32_t>(params.abv)>10)append(screens,13);
        if(valid_strength(recipe.parameters.ibu)&&static_cast<std::uint32_t>(params.ibu)>10)append(screens,14);
        HeadUiEvent load;load.command=static_cast<UiCommand>(11);load.flags=12;load.recipe=recipe;load.parameters=params;screens.menu_data.append(load,localized(screens,UiTextCategory::UiCommand,static_cast<std::uint32_t>(load.command)));
        if(valid_strength(recipe.parameters.abv)&&static_cast<std::uint32_t>(params.abv)<11)append(screens,13);
        if(valid_strength(recipe.parameters.ibu)&&static_cast<std::uint32_t>(params.ibu)<11)append(screens,14);
        append(screens,3);
        std::array<std::array<char,32>,3> buffer{};format_bounded(buffer[0].data(),32,"RecipeID: %d",static_cast<std::int32_t>(recipe.id));const char* lines[4]={buffer[0].data(),recipe.name.data(),nullptr,nullptr};std::uint8_t count=2;
        for(unsigned i=0;i<2;++i){const auto target=i?recipe.parameters.ibu:recipe.parameters.abv,actual=i?params.ibu:params.abv;const char* name=i?"IBU":"ABV";if(!valid_strength(target)&&target!=-1&&!assertion("Head.h",0x47a,"target == -1 || ProgramParams::isValidTarget(target)"))return false;if(actual!=-1&&(actual<0||actual>10)&&!assertion("Head.h",0x47b,"actual == -1 || ProgramParams::isValidTweak(actual)"))return false;
            if(!valid_strength(target))format_bounded(buffer[i+1].data(),32,"%s: %s",name,localized(screens,UiTextCategory::TextInfo,18));else if(actual<0||actual==target)format_bounded(buffer[i+1].data(),32,"%s: %d (%s)",name,target,localized(screens,UiTextCategory::TextInfo,13));else format_bounded(buffer[i+1].data(),32,actual>target?"%s: %d (+%d)":"%s: %d (-%d)",name,target,actual>target?actual-target:target-actual);
            if(valid_strength(target))lines[count++]=buffer[i+1].data();}
        return screen_result(body(screens,title(screens,52),lines,count));
    }
    case HeadUiEffect::ManualStepScreen:{
        screens.clear_data();const auto& value=numeric.state.manual;for(unsigned i=0;i<4;++i)if(!value.bytes_38[i])append(screens,96+i);append(screens,100);for(unsigned i=0;i<4;++i)if(value.bytes_38[i])append(screens,96+i);if(can_drain(observed.runtime))append(screens,62);append(screens,51);
        std::array<char,32> hold{},temperature{},drain{},line{};if(!head_quantity_text(hold.data(),32,value.hold,!settings->use_imperial)||!head_quantity_text(temperature.data(),32,value.temperature,!settings->use_imperial)||!head_quantity_text(drain.data(),32,value.drain,!settings->use_imperial))return false;
        screens.scroll_data.append(to_string(static_cast<ValveLocation>(value.location)));format_bounded(line.data(),32,"%s %s",hold.data(),temperature.data());screens.scroll_data.append(line.data());format_bounded(line.data(),32,"%s %s",localized(screens,UiTextCategory::TextCommon,27),drain.data());screens.scroll_data.append(line.data());return screen(19,60);
    }
    case HeadUiEffect::ResetRuntimeMenu:return runtime_menu();
    case HeadUiEffect::ConfirmStop:{screens.clear_data();append(screens,2,localized(screens,UiTextCategory::TextCommon,10));append(screens,52,localized(screens,UiTextCategory::TextCommon,first==55?28:19));const char* explanation=nullptr;ui_screen_text(static_cast<ScreenId>(first),screens.language,explanation);return screen_result(wrapped(screens,title(screens,first==55?62:61),explanation));}
    case HeadUiEffect::About:{
        screens.clear_data();std::array<char,16> firmware{},flash{};format_bounded(firmware.data(),firmware.size(),"%d.%d",observed.msp.version_major,observed.msp.version_minor);format_bounded(flash.data(),flash.size(),"%dMB",observed.flash_bytes/0x100000);
        const char* values[]={observed.identifier.data(),observed.backend_alias.data(),"0.0.119",to_string(settings->boiler),"Apr 17 2020","08:34:08","master","www.picobrew.com",firmware.data(),flash.data()};
        for(std::uint32_t i=0;i<10;++i)if(!aligned_line(localized(screens,UiTextCategory::TextAbout,i),values[i]))return false;
        HeadScreenRequest request;request.title=title(screens,1);request.screen.layout=6;request.scroll_alignment=DisplayTextAlignment::Left;return screen_result(screens.show(request));
    }
    case HeadUiEffect::HardwareScreen:return screen(0,0,UiCommand::Exit);
    default:status.result=HeadApplicationResult::RenderFailure;status.detail=static_cast<std::uint32_t>(effect);return false;
    }
}

bool HeadApplication::runtime_instruction(){
    const auto id=observed.runtime.screen;screens.clear_data();if(observed.core.azure_enabled)append(screens,67);
    if(id==41||id==42||id==94)append(screens,58);
    else if(id==74)append(screens,57);
    else if(id==71)append(screens,59);
    else if(id==89){append(screens,57);append(screens,62);}
    else if(id==10||id==11){append(screens,id==10?63:64);append(screens,60);append(screens,52);}
    else if(id!=13){if(id!=73)append(screens,57);append(screens,50);}
    if(id==42){HeadScreenRequest request;request.screen.layout=4;request.screen.command=static_cast<UiCommand>(83);request.screen.argument=static_cast<std::uint32_t>(observed.core.session_type);return screen_result(screens.show(request));}
    const char* explanation=nullptr;if(ui_screen_text(static_cast<ScreenId>(id),screens.language,explanation)!=UiScreenTextResult::Ok)return false;
    Text expanded{};std::array<char,20> value{};
    if(id==85||id==87){double liters=0;if(!quantity_in(numeric.state.parameters.water,Unit::Liter,liters))return false;Quantity quantity{liters/0.88,Unit::Liter};if(id==85)quantity={quantity.value*60,Unit::Gram};if(!head_quantity_text(value.data(),value.size(),quantity,!settings->use_imperial))return false;format_bounded(expanded.data(),128,explanation,value.data());explanation=expanded.data();}
    else if(id==9&&observed.session.parameters.water.value>0){static const char* alternatives[]={"Insert Step Filter, fill Keg with %s of water and attach the Hoses.","Insert Step Filter, fill Keg with %s of water and attach the Hoses.","Insert Step Filter, fill Keg with %s of water and attach the Hoses."};if(!head_quantity_text(value.data(),value.size(),observed.session.parameters.water,!settings->use_imperial))return false;format_bounded(expanded.data(),128,alternatives[static_cast<unsigned>(screens.language)],value.data());explanation=expanded.data();}
    else if(id==74){Quantity quantity{observed.runtime.still.boil_t3,Unit::Celsius};if(!head_quantity_text(value.data(),value.size(),quantity,!settings->use_imperial,1))return false;format_bounded(expanded.data(),128,explanation,value.data());explanation=expanded.data();}
    return screen_result(wrapped(screens,session_title(*this,observed.core.session_type),explanation,screens.menu_data.count?17:21));
}

bool HeadApplication::core_presentation(const HeadCoreAction& action){
    switch(action.effect){
    case HeadCoreEffect::NetworkConfiguration:return presentation(HeadUiEffect::NetworkConfiguration);
    case HeadCoreEffect::RunningScreen:return running();
    case HeadCoreEffect::LoadingErrorScreen:case HeadCoreEffect::NetworkErrorScreen:{screens.clear_data();const char* value=nullptr;ui_screen_text(static_cast<ScreenId>(action.effect==HeadCoreEffect::LoadingErrorScreen?56:57),screens.language,value);HeadWrappedText lines;if(!head_wrap_text(lines,value))return false;for(std::uint32_t i=0;i<lines.count;++i)screens.scroll_data.append(lines.lines[i].data());return screen(6,16);}
    case HeadCoreEffect::ExpiredRegistrationScreen:{const char* lines[]={"","","Click button to get","","registration code"};HeadScreenRequest request;request.screen.layout=21;request.screen.command=static_cast<UiCommand>(84);request.has_body=true;request.body.lines=lines;request.body.count=5;return screen_result(screens.show(request));}
    case HeadCoreEffect::SessionStartScreen:if(observed.core.session_mode==SessionMode::Unknown){screens.clear_data();append(screens,47);append(screens,48);return screen_result(screens.instruction_menu(static_cast<ScreenId>(81),title(screens,49)));}return screen_result(screens.waiting(title(screens,49),nullptr,true));
    case HeadCoreEffect::ProgramLoadedScreen:{
        if(!action.event)return false;
        screens.clear_data();append(screens,action.event->type==CoreEventType::ResumedProgramLoaded?91:46);append(screens,observed.loaded_program_status==3?87:3);
        std::array<char,32> duration{},recipe{};const auto hours=observed.loaded_duration/3600,minutes=observed.loaded_duration%3600/60;if(hours)format_bounded(duration.data(),32,"%d %s %d %s",hours,localized(screens,UiTextCategory::TextTime,5),minutes,localized(screens,UiTextCategory::TextTime,8));else format_bounded(duration.data(),32,"%d %s",minutes,localized(screens,UiTextCategory::TextTime,7));
        format_bounded(recipe.data(),32,"RecipeID: %d",static_cast<std::int32_t>(observed.loaded_recipe.id));const char* lines[]={observed.loaded_recipe.name.data(),duration.data(),recipe.data()};return screen_result(body(screens,session_title(*this,observed.loaded_session_type),lines,static_cast<std::uint32_t>(observed.loaded_session_type)-6u<2?3:2));
    }
    case HeadCoreEffect::BoilerResultScreen:{
        if(!action.event)return false;
        screens.clear_data();const auto& event=*action.event;const char* lines[2]={nullptr,nullptr};
        if(event.detection_error==HexDetectError::None){lines[0]="Boiler Type:";lines[1]=to_string(event.boiler);const auto boiler=static_cast<std::uint32_t>(event.boiler);if(boiler>1&&!assertion("Head.h",0x59c,"ev.boilerType == BoilerType::Small || ev.boilerType == BoilerType::Big"))return false;if(boiler<2)append(screens,boiler?140:139);}
        else{lines[0]="HEX Detection Error:";lines[1]=to_string(event.detection_error);}append(screens,138);append(screens,1);return screen_result(body(screens,title(screens,6),lines,2));
    }
    case HeadCoreEffect::RecipeListScreen:return action.event&&recipe_list(*action.event);
    case HeadCoreEffect::SystemErrorScreen:{
        screens.clear_data();std::array<char,32> line{};
        if(observed.error_text[0]){format_bounded(line.data(),32,"SYS %s",observed.error_text.data());screens.scroll_data.append(line.data());}
        if(observed.backend_error){const char* value=observed.backend_error_text[0]?observed.backend_error_text.data():observed.registered?"":"UID is unknown";format_bounded(line.data(),32,"NET %s",value);screens.scroll_data.append(line.data());if(observed.backend_error_code){format_bounded(line.data(),32,"BackendError %d",static_cast<std::int32_t>(observed.backend_error_code));screens.scroll_data.append(line.data());}}
        if(observed.msp.id!=2||observed.msp.errors){format_bounded(line.data(),32,"MSP %s",to_string(static_cast<MspStateId>(observed.msp.id)));screens.scroll_data.append(line.data());format_bounded(line.data(),32,"AppError 0x%x",observed.msp.errors);screens.scroll_data.append(line.data());}
        if(observed.sensor_error[0]){format_bounded(line.data(),32,"SENSORS %s",observed.sensor_error.data());screens.scroll_data.append(line.data());}
        HeadScreenRequest request;request.title=title(screens,16);request.screen.layout=6;request.custom_scroll=true;request.scroll_alignment=DisplayTextAlignment::Center;return screen_result(screens.show(request));
    }
    default:return false;
    }
}

bool HeadApplication::recipe_list(const CoreEvent& event){
    if(event.type!=CoreEventType::RecipeRefListLoaded&&!assertion("Head.h",0x684,"ev.Type == CoreEventType::RecipeRefListLoaded"))return false;
    if(event.count>event.recipes.size())return false;
    screens.clear_data();const auto source=static_cast<std::uint32_t>(event.source);std::uint32_t heading=25;
    if(source==10||source==11)heading=30;else if(source==4)heading=59;else if(source==5||source==7)heading=58;else if(source!=3&&!assertion("Head.h",0x69d,"ev.source == CoreCommandId::MyRecipeListGet"))return false;
    if(source==3&&!event.count){append(screens,103);const char* line=localized(screens,UiTextCategory::TextCommon,12);return screen_result(body(screens,title(screens,heading),&line,1));}
    if(source==7||source==10){
        if(event.count!=1&&!assertion("Head.h",0x6aa,"ev.count == 1"))return false;
        const auto& recipe=event.recipes[0];
        if(recipe.kind==RecipeKind::PicoPak){if(!valid_strength(recipe.parameters.abv)&&!valid_strength(recipe.parameters.ibu)){HeadUiCommandData data;data.flags=12;data.recipe=recipe;data.parameters=numeric.state.parameters;CoreCommand command;command.id=CoreCommandId::PicoPakRecipeLoad;command.program_parameters=data.parameters;return post(command,&data);}numeric.state.parameters=ProgramParameters{};ui.state.recipe=recipe;return presentation(HeadUiEffect::RecipeStrengthScreen);}
        if(recipe.kind==RecipeKind::Zymatic){HeadUiEvent item;item.command=static_cast<UiCommand>(8);item.flags=4;item.recipe=recipe;screens.menu_data.append(item,recipe.name.data());}
    }else{
        std::uint32_t reserve=(event.count?1:0)+(event.offset?1:0)+(source==11?1:source==4?2:0);std::int32_t count=static_cast<std::int32_t>(std::min<std::uint32_t>(event.count,32-(reserve+1)));const bool next=count+event.offset<event.total;if(next)--count;
        if(count<0){if(!assertion("Head.h",0x6d9,"displayedRecipesCount >= 0"))return false;count=0;}
        if(source!=5&&event.count&&!text.state.kit_length)append(screens,1);
        if(event.offset){if(event.offset<20&&!assertion("Head.h",0x6e3,"ev.offset >= ev.list.max_size()"))return false;HeadUiEvent previous;previous.command=static_cast<UiCommand>(4);previous.flags=text.state.kit_length?17:1;previous.index=event.offset<20?0:event.offset-20;screens.menu_data.append(previous,"Prev 20");}
        for(std::int32_t i=0;i<count;++i){const auto& recipe=event.recipes[static_cast<std::size_t>(i)];const auto kind=static_cast<std::uint32_t>(recipe.kind);if(kind>2)continue;HeadUiEvent item;item.command=static_cast<UiCommand>(kind==0?8:kind==2?9:valid_strength(recipe.parameters.abv)||valid_strength(recipe.parameters.ibu)?12:10);item.flags=4;item.recipe=recipe;screens.menu_data.append(item,recipe.name.data());}
        if(next){const auto index=static_cast<std::uint32_t>(count)+event.offset+1;const auto remaining=static_cast<std::int32_t>(event.total-index);std::array<char,32> label{};format_bounded(label.data(),label.size(),"Next %d",std::min(remaining,count));HeadUiEvent item;item.command=static_cast<UiCommand>(4);item.flags=text.state.kit_length?17:1;item.index=index;screens.menu_data.append(item,label.data());}
        if(source==11)append(screens,16);else if(source==5){append(screens,17);append(screens,6);append_index(screens,4,0,localized(screens,UiTextCategory::UiCommand,4));}
    }
    append(screens,1);const char* heading_text=text.state.kit_length?(event.count?text.state.kit_code.data():localized(screens,UiTextCategory::TextCommon,21)):title(screens,heading);return screen_result(screens.menu_screen(heading_text));
}

bool HeadApplication::network_action(const HeadNetworkAction& action){
    switch(action.effect){
    case HeadNetworkEffect::ResetScreen:screens.clear_data();return true;
    case HeadNetworkEffect::AppendLine:return screens.scroll_data.append(action.text);
    case HeadNetworkEffect::AppendCommand:return append(screens,action.first);
    case HeadNetworkEffect::PostCoreCommand:return post(static_cast<CoreCommandId>(action.first));
    case HeadNetworkEffect::HomeScreen:return screen_result(screens.home());
    case HeadNetworkEffect::ScanningMessage:return screen_result(screens.waiting(action.title,nullptr,true));
    case HeadNetworkEffect::WifiErrorScreen:{HeadScreenRequest request;request.title=action.title;request.screen.layout=6;return screen_result(screens.show(request));}
    case HeadNetworkEffect::GroupSessionScreen:{const char* lines[]={action.text};return screen_result(body(screens,action.title,lines,1));}
    case HeadNetworkEffect::WifiScanResults:{
        screens.clear_data();if(static_cast<std::int32_t>(action.third)<1){append(screens,103);const char* line=localized(screens,UiTextCategory::TextCommon,11);return screen_result(body(screens,title(screens,57),&line,1));}
        if(!services.scan_network){status.result=HeadApplicationResult::MissingBinding;return false;}append(screens,103);std::array<char,33> previous{};bool have_previous=false;
        for(std::uint32_t i=0;i<action.third&&screens.menu_data.count<31;++i){HeadWifiNetwork network;if(!services.scan_network(services.context,i,network))return false;if(network.rssi<-84||(have_previous&&network.ssid==previous))continue;Text label{};format_bounded(label.data(),label.size(),"%s%s%s",network.encrypted?"\xc2\x82 ":"",network.rssi<-75?"[weak] ":"",network.ssid.data());append_index(screens,108,i,label.data());previous=network.ssid;have_previous=true;}
        append(screens,103);return screen_result(screens.menu_screen(title(screens,57)));
    }
    }return false;
}
}} // namespace zseries::oem
