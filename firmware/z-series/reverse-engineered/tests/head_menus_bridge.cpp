#include "zseries/head_application.hpp"
#include <cstring>
#include <vector>
using namespace zseries::oem;
using Word=std::uint32_t;
namespace {struct Context{Word count=7,line=0;};Word clock(void*){return 777;}}
extern "C" unsigned head_menus_test(const Word* args,const double* temperatures,const char* title,const char* detail,const char* button,
 const char* const* labels,Word label_count,const char* const* lines,Word line_count,const char* const* scroll,Word scroll_count,
 unsigned char* pixels,Word* out,char* menu_text,Word* menu_commands,char* scroll_text){
    HeadApplication app;EepromData settings;app.bind_settings(settings);auto& display=app.display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<unsigned char>((i*73+17)^(i>>3));
    auto& encoder=app.encoder.state;auto& periodic=app.periodic.state;auto& screens=app.screens;Context context;
    settings.language=static_cast<LanguageType>(args[2]);settings.use_imperial=args[3];settings.use_ethernet=1;settings.show_detail=args[36];settings.slideshow_cycle_static=args[37];settings.azure_enabled=static_cast<BoolOption>(args[38]);settings.still_session_mode=static_cast<SessionMode>(args[38]);settings.boiler=static_cast<BoilerType>(args[38]);std::strcpy(settings.wifi_ssid.data(),"saved-network");std::strcpy(settings.wifi_password.data(),"saved-password");
    app.services.context=&context;app.services.millis=clock;app.services.alarm=[](void*,bool){return true;};app.services.assertion=[](void* p,const char*,Word line,const char*){auto& c=*static_cast<Context*>(p);++c.count;c.line=line;return true;};
    auto& o=app.observed;o.core.id=CoreStateId::Idle;o.core.service_mode=args[18];o.core.offline=args[19];o.network=args[35];o.ethernet_address=0x962ba8c0;o.wifi.local_address=0x642ba8c0;o.wifi.gateway_address=0x012ba8c0;o.wifi.rssi=-42;
    o.flash_bytes=0x800000;o.msp.version_major=3;o.msp.version_minor=4;std::strcpy(o.backend_alias.data(),"Kitchen Z");std::strcpy(o.identifier.data(),"Z119FIXTURE");std::strcpy(o.wifi.connected.ssid.data(),"12345678901234567890123456789012");std::strcpy(o.wifi_bssid.data(),"01:23:45:67:89:AB");for(unsigned i=0;i<6;++i){o.wifi_mac[i]=0x10+i;o.ethernet_mac[i]=0x20+i;}
    app.refresh_observed();
    screens.observed.imperial=args[3];screens.observed.temperature=temperatures[0];screens.observed.minimum_temperature=temperatures[1];screens.observed.maximum_temperature=temperatures[2];screens.observed.duration=args[16];screens.observed.session=static_cast<SessionType>(args[17]);screens.observed.service_mode=args[18];screens.observed.offline=args[19];
    if(args[0]==0&&(args[1]==0||args[1]==1||args[1]==14||args[1]==16)){auto& o=static_cast<HeadDynamicObserved&>(screens.observed);
        o.core.session_type=static_cast<decltype(o.core.session_type)>(6);
        o.heater.started=static_cast<decltype(o.heater.started)>(1);
        o.msp.status_byte_3=static_cast<decltype(o.msp.status_byte_3)>(3);
        o.msp.valve_position=static_cast<decltype(o.msp.valve_position)>(4);
        o.msp.moving_valve=static_cast<decltype(o.msp.moving_valve)>(0);
        o.msp.pumps=static_cast<decltype(o.msp.pumps)>(112);
        o.msp.fan=static_cast<decltype(o.msp.fan)>(8);
        o.msp.errors=static_cast<decltype(o.msp.errors)>(0);
        o.safety.filter_missing=static_cast<decltype(o.safety.filter_missing)>(0);
        o.runtime.id=static_cast<decltype(o.runtime.id)>(3);
        o.runtime.elapsed_seconds=static_cast<decltype(o.runtime.elapsed_seconds)>(8345);
        o.runtime.instruction_left_seconds=static_cast<decltype(o.runtime.instruction_left_seconds)>(234);
        o.runtime.instruction_count=static_cast<decltype(o.runtime.instruction_count)>(23);
        o.runtime.instruction.opcode=static_cast<decltype(o.runtime.instruction.opcode)>(4);
        o.runtime.instruction.argument=static_cast<decltype(o.runtime.instruction.argument)>(600);
        o.still.id=static_cast<decltype(o.still.id)>(19);
        o.still_connectivity_error=static_cast<decltype(o.still_connectivity_error)>(0);
        o.heater.target_celsius=81.5;
        o.heater.output_ticks=45.5;
        o.heater_sensors.temperatures[0].celsius=91.25;
        o.heater_sensors.temperatures[1].celsius=78.5;
        o.heater_sensors.temperatures[2].celsius=23.4;
        o.heater_sensors.temperatures[3].celsius=19.75;
        o.auxiliary.acceleration[0]=1;
        o.auxiliary.acceleration[1]=0.03;
        o.auxiliary.acceleration[2]=0.04;
        std::strcpy(o.session.name.data(),"Recipe name");o.runtime.step_name="Mash";o.runtime.substep_name="Circulating";std::strcpy(o.still_samples.current.error.data(),"Still sample error");o.runtime.pause_reason=args[32];
    }
    encoder.diagnostic_line=static_cast<std::uint8_t>(args[33]);
    for(Word i=0;i<label_count;++i)screens.menu_data.append(static_cast<UiCommand>(i==0&&args[24]?1:10+i),labels[i]);
    for(Word i=0;i<scroll_count;++i)screens.scroll_data.append(scroll[i]);
    HeadScreenRequest request;request.screen.layout=args[1];request.title=title;request.screen.selection=args[4];request.screen.strength=args[5];request.counter=static_cast<std::int16_t>(args[6]);request.animate=args[7];request.percentage=args[8];request.menu_rows=args[9];request.menu_vertical=args[10];request.body.horizontal=static_cast<DisplayTextAlignment>(args[11]);request.body.vertical=args[12];request.custom_scroll=args[13];request.scroll_command=static_cast<UiCommand>(args[14]);request.scroll_alignment=static_cast<DisplayTextAlignment>(args[15]);request.scroll_button=button;request.screen.command=static_cast<UiCommand>(args[22]);request.screen.argument=args[23];request.body.lines=lines;request.body.count=line_count;request.has_body=args[25];
    request.screen.input_kind=args[27];request.screen.input_limit=args[28];request.screen.response=static_cast<UiCommand>(args[29]);request.screen.quantity_id=args[30];request.screen.quantity={temperatures[3],static_cast<Unit>(args[31])};
    HeadScreenResult result=HeadScreenResult::Ok;
    switch(args[0]){
    case 0:result=screens.show(request);break;
    case 1:{HeadWrappedText wrapped;if(!head_wrap_text(wrapped,title,args[26]))return 99;out[0]=wrapped.count;out[1]=wrapped.truncated;for(Word i=0;i<wrapped.count;++i)std::memcpy(scroll_text+i*65,wrapped.lines[i].data(),43);return 0;}
    case 2:result=screens.home();break;
    case 3:result=screens.instruction_menu(static_cast<ScreenId>(args[20]),ui_localized_text(UiTextCategory::TextTitle,args[21],screens.language));break;
    case 4:result=screens.waiting(title,detail,args[7],args[6]);break;
    case 5:result=screens.registration(detail);break;
    case 6:result=screens.diagnostic_paragraph(static_cast<std::int32_t>(args[34]));break;
    case 7:{HeadUiEvent value;value.command=static_cast<UiCommand>(args[39]);value.flags=1;value.index=5;app.receive_ui(value);if(!app.consume(HeadEventKind::UiCommand))return 90+static_cast<unsigned>(app.status.result);break;}
    default:return 98;
    }
    if(result!=HeadScreenResult::Ok)return static_cast<unsigned>(result);
    const auto& s=screens.state;const auto& p=screens.paragraph.state;
    Word values[]={static_cast<Word>(s.available.left),static_cast<Word>(s.available.top),static_cast<Word>(s.available.right),static_cast<Word>(s.available.bottom),static_cast<Word>(s.default_command),s.reset_time,s.layout_marker,
      encoder.menu.enabled,encoder.menu.selected,encoder.menu.visible_rows,encoder.menu.selection_row,encoder.menu.scroll_offset,
      encoder.paragraph.enabled,static_cast<Word>(encoder.paragraph.offset),static_cast<Word>(encoder.paragraph.visible_lines),p.deadline,p.caret_visible,
      encoder.temperature.enabled,static_cast<Word>(encoder.temperature.value),static_cast<Word>(encoder.temperature.minimum),static_cast<Word>(encoder.temperature.maximum),s.metric,
      encoder.duration.enabled,encoder.duration.seconds,static_cast<Word>(s.duration_session),encoder.strength.enabled,static_cast<Word>(encoder.strength.value),
      encoder.quantities[0].enabled,static_cast<Word>(encoder.quantities[0].selected),static_cast<Word>(encoder.quantities[0].count),encoder.quantities[0].wrap,
      periodic.waiting.enabled,periodic.waiting.animate,periodic.waiting.frame,periodic.waiting.frame_count,periodic.waiting.deadline,
      s.title_height,s.title_underline,s.title_enabled,encoder.combined.enabled,encoder.combined.selected,encoder.combined.paragraph_count,encoder.combined.menu_count,
      screens.painter.dirty,context.line,context.count};
    std::memcpy(out,values,sizeof values);out[46]=screens.menu_data.count;out[47]=screens.scroll_data.count;
    for(Word i=0;i<screens.menu_data.count;++i){std::memcpy(menu_text+i*129,screens.menu_data.items[i].text.data(),129);menu_commands[i]=static_cast<Word>(screens.menu_data.items[i].event.command);}
    for(Word i=0;i<screens.scroll_data.count;++i)std::memcpy(scroll_text+i*65,screens.scroll_data.lines[i].data(),65);
    if(args[0]==0&&args[1]==8){Word values[]={encoder.character.enabled,static_cast<Word>(encoder.character.count),static_cast<Word>(encoder.character.selected),screens.character.state.kind,screens.character.state.visible,screens.character.state.limit,static_cast<Word>(screens.character.state.response),screens.character.state.length};std::memcpy(out+48,values,sizeof values);}
    if(args[0]==0&&args[1]==12){const auto& selected=encoder.quantities[1];const auto& quantity=screens.quantity.state;Word values[]={selected.enabled,static_cast<Word>(selected.selected),static_cast<Word>(selected.count),selected.wrap,static_cast<Word>(quantity.id),static_cast<Word>(quantity.unit),quantity.metric};std::memcpy(out+48,values,sizeof values);std::memcpy(out+56,&quantity.minimum,8);std::memcpy(out+58,&quantity.step,8);}
    const Word dynamic[]={periodic.runtime_state.enabled,periodic.runtime_state.deadline,periodic.runtime_transient.enabled,periodic.runtime_transient.deadline,periodic.level.enabled,periodic.level.deadline,periodic.pause.enabled,periodic.pause.deadline,static_cast<Word>(s.runtime_info_command),static_cast<Word>(s.pause_reason)};std::memcpy(out+60,dynamic,sizeof dynamic);out[70]=encoder.diagnostic_line;
    std::memcpy(pixels,display.pixels.data(),display.pixels.size());return 0;
}
