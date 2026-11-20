#include "zseries/head_screens.hpp"
#include <cstring>
#include <vector>
using namespace zseries::oem;
using Word=std::uint32_t;
namespace {struct Context{Word count=7;};bool assertion(void* p,Word){++static_cast<Context*>(p)->count;return true;}Word clock(void*){return 777;}}
extern "C" unsigned head_screens_test(const Word* args,const double* temperatures,const char* title,const char* detail,const char* button,
 const char* const* labels,Word label_count,const char* const* lines,Word line_count,const char* const* scroll,Word scroll_count,
 unsigned char* pixels,Word* out,char* menu_text,Word* menu_commands,char* scroll_text){
    DisplayState display;for(std::size_t i=0;i<display.pixels.size();++i)display.pixels[i]=static_cast<unsigned char>((i*73+17)^(i>>3));
    HeadEncoderState encoder;UiScreenTickState periodic;HeadScreens screens(display,encoder,periodic);Context context;screens.services.context=&context;screens.services.millis=clock;screens.services.assertion=assertion;screens.language=static_cast<LanguageType>(args[2]);
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
      screens.painter.dirty,screens.painter.assertion_line,context.count};
    std::memcpy(out,values,sizeof values);out[46]=screens.menu_data.count;out[47]=screens.scroll_data.count;
    for(Word i=0;i<screens.menu_data.count;++i){std::memcpy(menu_text+i*129,screens.menu_data.items[i].text.data(),129);menu_commands[i]=static_cast<Word>(screens.menu_data.items[i].event.command);}
    for(Word i=0;i<screens.scroll_data.count;++i)std::memcpy(scroll_text+i*65,screens.scroll_data.lines[i].data(),65);
    if(args[0]==0&&args[1]==8){Word values[]={encoder.character.enabled,static_cast<Word>(encoder.character.count),static_cast<Word>(encoder.character.selected),screens.character.state.kind,screens.character.state.visible,screens.character.state.limit,static_cast<Word>(screens.character.state.response),screens.character.state.length};std::memcpy(out+48,values,sizeof values);}
    if(args[0]==0&&args[1]==12){const auto& selected=encoder.quantities[1];const auto& quantity=screens.quantity.state;Word values[]={selected.enabled,static_cast<Word>(selected.selected),static_cast<Word>(selected.count),selected.wrap,static_cast<Word>(quantity.id),static_cast<Word>(quantity.unit),quantity.metric};std::memcpy(out+48,values,sizeof values);std::memcpy(out+56,&quantity.minimum,8);std::memcpy(out+58,&quantity.step,8);}
    const Word dynamic[]={periodic.runtime_state.enabled,periodic.runtime_state.deadline,periodic.runtime_transient.enabled,periodic.runtime_transient.deadline,periodic.level.enabled,periodic.level.deadline,periodic.pause.enabled,periodic.pause.deadline,static_cast<Word>(s.runtime_info_command),static_cast<Word>(s.pause_reason)};std::memcpy(out+60,dynamic,sizeof dynamic);out[70]=encoder.diagnostic_line;
    std::memcpy(pixels,display.pixels.data(),display.pixels.size());return 0;
}
extern "C" unsigned head_screens_integration(){
    struct Owner{DisplayState display;HeadEncoder encoder;UiScreenTick periodic;HeadScreens screens;
        PendingEvent<HeadScreenEvent> events;Word now=1000;
        Owner():screens(display,encoder.state,periodic.state){}
    } owner;
    auto& screens=owner.screens;
    screens.services.context=&owner;screens.services.millis=[](void* p){return static_cast<Owner*>(p)->now;};
    screens.services.assertion=[](void*,Word){return true;};
    screens.services.post_event=[](void* p,const HeadScreenEvent& event){static_cast<Owner*>(p)->events.put(event);return true;};
    owner.encoder.services.context=&owner;
    owner.encoder.services.millis=[](void* p){return static_cast<Owner*>(p)->now;};
    owner.encoder.services.perform=[](void* p,HeadEncoderEffect effect,Word first,Word second){return static_cast<Owner*>(p)->screens.encoder_effect(effect,first,second)==HeadScreenResult::Ok;};
    owner.periodic.services.context=&owner;
    owner.periodic.services.perform=[](void* p,UiScreenTickEffect effect,Word first,Word second){return static_cast<Owner*>(p)->screens.tick_effect(effect,first,second)==HeadScreenResult::Ok;};
    if(screens.home()!=HeadScreenResult::Ok)return 1;
    if(owner.encoder.rotate(-1)!=HeadEncoderResult::Ok||owner.encoder.state.menu.selected!=1)return 2;
    if(screens.button()!=HeadScreenResult::Ok||!owner.events.pending||owner.events.value.ui.command!=UiCommand::PacksMenu)return 3;
    // The posted selection remains owned even as screen/menu storage changes.
    HeadUiEvent recipe;recipe.command=static_cast<UiCommand>(8);recipe.flags=4;recipe.recipe.id=765;std::memcpy(recipe.recipe.name.data(),"Owned recipe",13);
    screens.clear_data();screens.menu_data.append(recipe,"Recipe");
    if(screens.menu_screen("Recipes")!=HeadScreenResult::Ok)return 4;
    owner.events.pending=false;
    if(screens.button()!=HeadScreenResult::Ok)return 5;
    screens.clear_data();recipe.recipe.id=999;
    if(owner.events.value.ui.recipe.id!=765||std::strcmp(owner.events.value.ui.recipe.name.data(),"Owned recipe"))return 6;
    HeadScreenRequest request;request.screen.layout=7;screens.observed.temperature=80;screens.observed.minimum_temperature=50;screens.observed.maximum_temperature=100;
    if(screens.show(request)!=HeadScreenResult::Ok||owner.encoder.rotate(-2)!=HeadEncoderResult::Ok)return 7;
    owner.events.pending=false;if(screens.button()!=HeadScreenResult::Ok||owner.events.value.kind!=HeadScreenEventKind::Temperature||owner.events.value.temperature!=82)return 8;
    const char* lines[]={"Please wait","Working"};request=HeadScreenRequest{};request.screen.layout=13;request.has_body=true;request.body.lines=lines;request.body.count=2;request.animate=true;
    if(screens.show(request)!=HeadScreenResult::Ok)return 9;
    const auto initial=owner.display.pixels;
    owner.now=1500;if(owner.periodic.tick(owner.now)!=UiScreenTickResult::Ok||owner.periodic.state.waiting.frame)return 10;
    owner.now=1501;if(owner.periodic.tick(owner.now)!=UiScreenTickResult::Ok||owner.periodic.state.waiting.frame!=1||owner.display.pixels==initial)return 11;
    screens.clear_data();for(Word i=0;i<12;++i)screens.scroll_data.append("Long instructions");screens.menu_data.append(UiCommand::Back);
    request=HeadScreenRequest{};request.screen.layout=20;if(screens.show(request)!=HeadScreenResult::Ok)return 12;
    if(owner.encoder.rotate(-1)!=HeadEncoderResult::Ok||owner.encoder.state.paragraph.offset!=1)return 13;
    owner.now=2000;if(owner.periodic.tick(owner.now)!=UiScreenTickResult::Ok||owner.periodic.state.combined_caret.deadline!=2125)return 14;
    request=HeadScreenRequest{};request.screen.layout=8;request.screen.input_kind=0;request.screen.input_limit=3;request.screen.response=UiCommand::ZymaticRecipeInput;
    if(screens.show(request)!=HeadScreenResult::Ok||screens.button()!=HeadScreenResult::Ok||screens.character.state.length!=1)return 15;
    owner.encoder.state.character.selected=12;owner.events.pending=false;if(screens.button()!=HeadScreenResult::Ok||owner.events.value.kind!=HeadScreenEventKind::Text||owner.events.value.ui.command!=UiCommand::ZymaticRecipeInput||std::strcmp(owner.events.value.text.data(),"1"))return 16;
    request=HeadScreenRequest{};request.screen.layout=12;request.screen.quantity_id=1;request.screen.quantity={80,Unit::Celsius};if(screens.show(request)!=HeadScreenResult::Ok||owner.encoder.rotate(-2)!=HeadEncoderResult::Ok)return 17;
    owner.events.pending=false;if(screens.button()!=HeadScreenResult::Ok||owner.events.value.kind!=HeadScreenEventKind::Quantity||owner.events.value.quantity_id!=HeadQuantityId::CoffeeTemperature||owner.events.value.quantity.value!=82)return 18;
    return 0;
}
