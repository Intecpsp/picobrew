#include "zseries/head_ui_commands.hpp"
#include <cstring>

namespace zseries { namespace oem {
#define HEAD_UI_TRY(x) do {const auto status=(x);if(status!=HeadUiResult::Ok)return status;}while(false)
HeadUiResult HeadUiCommands::perform(const HeadUiAction& action){
    if(!services.perform)return HeadUiResult::MissingService;
    return services.perform(services.context,action)?HeadUiResult::Ok:HeadUiResult::ServiceFailure;
}
HeadUiResult HeadUiCommands::effect(HeadUiEffect kind,std::uint32_t first,std::uint32_t second,std::uint32_t third){HeadUiAction action(kind);action.first=first;action.second=second;action.third=third;return perform(action);}
HeadUiResult HeadUiCommands::assertion(std::uint32_t line){state.assertion_line=line;if(!services.assertion)return HeadUiResult::MissingService;return services.assertion(services.context,line)?HeadUiResult::Ok:HeadUiResult::AssertionFailure;}
HeadUiResult HeadUiCommands::post(CoreCommand& command,const HeadUiCommandData* data){HeadUiAction action(HeadUiEffect::PostCoreCommand);command.detail=data;action.core_command=&command;action.data=data;return perform(action);}
HeadUiResult HeadUiCommands::screen(const char* title,const HeadUiScreen& screen){HeadUiAction action(HeadUiEffect::Screen);action.title=title;action.screen=screen;return perform(action);}
HeadUiResult HeadUiCommands::screen(std::uint32_t title,const HeadUiScreen& value){return screen(ui_localized_text(UiTextCategory::TextTitle,title,language),value);}
HeadUiResult HeadUiCommands::menu(std::uint32_t title,const std::uint32_t* commands,std::size_t count){
    HEAD_UI_TRY(effect(HeadUiEffect::ResetScreen));for(std::size_t i=0;i<count;++i)HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,commands[i]));
    HeadUiAction action(HeadUiEffect::Menu);action.title=ui_localized_text(UiTextCategory::TextTitle,title,language);action.first=count;action.second=1;return perform(action);
}
HeadUiResult HeadUiCommands::start(std::uint32_t id){CoreCommand command;command.id=static_cast<CoreCommandId>(13);command.program_id=id;HEAD_UI_TRY(post(command));HeadUiAction action(HeadUiEffect::WaitingMessage);action.title=ui_localized_text(UiTextCategory::TextTitle,33,language);return perform(action);}
HeadUiResult HeadUiCommands::recipe_command(const HeadUiEvent& event){
    if(!(event.flags&4))HEAD_UI_TRY(assertion(0x8c6));
    // hasRecipeRef is checked independently by the getter, even after the first
    // returning assertion. Both original diagnostics are preserved.
    if(!(event.flags&4))HEAD_UI_TRY(assertion(0x1a8));
    CoreCommand command;HeadUiCommandData data;data.flags=4;data.recipe=event.recipe;
    const auto id=static_cast<std::uint32_t>(event.command);command.id=static_cast<CoreCommandId>(id==8?6:id==9?8:9);
    if(id==10||id==11){data.flags|=8;data.parameters=numeric.parameters;command.program_parameters=numeric.parameters;}
    return post(command,&data);
}
HeadUiResult HeadUiCommands::consume(PendingEvent<HeadUiEvent>& mailbox){mailbox.pending=false;const HeadUiEvent event=mailbox.value;return handle(event);}
HeadUiResult HeadUiCommands::handle(const HeadUiEvent& event){
    HEAD_UI_TRY(effect(HeadUiEffect::Alarm,0));transient.active=false;transient.deadline=0;
    const auto id=static_cast<std::uint32_t>(event.command);
    struct Route{std::uint16_t ui,core;};
    static const Route commands[]={
        {5,3},{15,11},{49,25},{47,16},{48,15},{52,26},{53,26},{55,27},
        {56,28},{57,28},{58,28},{59,28},{60,28},{61,29},{62,30},{63,31},{64,32},
        {133,36},{136,37},{93,52},{94,53},{84,38},{85,39},{110,40},{106,41},
        {23,19},{24,20},{25,21},{26,22},{28,23},{29,24},{138,43},{139,44},{140,45},
        {88,49},{89,49},{90,50},{91,51}};
    for(const auto& route:commands)if(id==route.ui)return effect(HeadUiEffect::DispatchCoreCommand,route.core);
    static const Route programs[]={
        {71,1},{72,12},{73,2},{74,3},{75,4},{76,27},{82,28},{77,5},{78,6},
        {142,14},{143,15},{144,16},{145,25},{146,17},{147,2},{148,18},{149,19},
        {79,26},{80,7},{46,24},{81,13}};
    for(const auto& route:programs)if(id==route.ui)return start(route.core);
    if(id>=8&&id<=11)return recipe_command(event);
    HeadUiScreen info;
    switch(id){
    case 4:{
        CoreCommand command;command.id=static_cast<CoreCommandId>(4);HeadUiCommandData data;
        if(!(event.flags&1))HEAD_UI_TRY(assertion(0x1a6));
        data.index=event.index;
        if(event.flags&16){data.search=text.kit_code;data.has_search=true;}
        else if(text.kit_length){text.kit_code.fill(0);text.kit_length=0;text.kit_truncated=false;}
        return post(command,&data);
    }
    case 40:{
        const auto program=static_cast<std::uint32_t>(numeric.parameters.id);
        if(program<20||program>23)HEAD_UI_TRY(assertion(0x8b4));
        CoreCommand command;command.id=static_cast<CoreCommandId>(2);command.program_parameters=numeric.parameters;
        HeadUiCommandData data;data.flags=8;data.parameters=numeric.parameters;return post(command,&data);
    }
    case 21:{
        if(!(event.flags&1))HEAD_UI_TRY(assertion(0x1a6));
        CoreCommand command;command.id=static_cast<CoreCommandId>(17);command.still_id=event.index;
        HEAD_UI_TRY(post(command));return effect(HeadUiEffect::Synchronize);
    }
    case 22:{CoreCommand command;command.id=static_cast<CoreCommandId>(18);HEAD_UI_TRY(post(command));return effect(HeadUiEffect::Synchronize);}
    case 65:{CoreCommand command;command.id=static_cast<CoreCommandId>(34);command.duration=0;return post(command);}
    case 104:case 105:return effect(HeadUiEffect::ConnectWifi);
    case 1:case 2:return effect(HeadUiEffect::Back);
    case 3:case 67:return effect(HeadUiEffect::Synchronize);
    case 87:HEAD_UI_TRY(effect(HeadUiEffect::Synchronize));return effect(HeadUiEffect::DispatchCoreCommand,47);
    case 68:info.layout=7;return screen(2,info);
    case 69:case 66:info.layout=9;info.argument=5;return screen(id==69?3:15,info);
    case 70:info.layout=11;info.selection=observed.runtime_location;return screen(45,info);
    case 30:{static const std::uint32_t list[]={78,77,1};return menu(48,list,3);}
    case 31:return effect(HeadUiEffect::CoffeeMenu);
    case 7:case 18:{
        HEAD_UI_TRY(effect(HeadUiEffect::ResetScreen));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,15));
        HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,id==7?19:16));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,1));
        HeadUiAction action(HeadUiEffect::Menu);action.title=ui_localized_text(UiTextCategory::TextTitle,id==7?27:30,language);action.second=1;return perform(action);
    }
    case 20:HEAD_UI_TRY(effect(HeadUiEffect::ResetScreen));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,79));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,80));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,1));return effect(HeadUiEffect::InstructionMenu,77,31);
    case 19:{CoreCommand command;command.id=static_cast<CoreCommandId>(5);HEAD_UI_TRY(post(command));if(text.kit_length){text.kit_code.fill(0);text.kit_length=0;text.kit_truncated=false;}return HeadUiResult::Ok;}
    case 41:{static const std::uint32_t list[]={71,72,73,74,75,76,3};return menu(55,list,7);}
    case 42:{static const std::uint32_t regular[]={102,115,111,118,121,126,129,3},service[]={102,115,111,118,121,126,132,134,135,137,3};return menu(46,observed.service_mode?service:regular,observed.service_mode?11:8);}
    case 45:return effect(HeadUiEffect::About);
    case 86:info.layout=14;info.command=UiCommand::Back;return screen("",info);
    case 102:case 103:return effect(HeadUiEffect::NetworkConfiguration);
    case 109:return effect(HeadUiEffect::EthernetConfiguration);
    case 92:HEAD_UI_TRY(effect(HeadUiEffect::ResetScreen));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,2));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,93));return effect(HeadUiEffect::InstructionMenu,100,41);
    case 118:{static const std::uint32_t list[]={119,120,3};return menu(47,list,3);}
    case 121:return effect(HeadUiEffect::SlideshowCycleConfiguration);
    case 107:return effect(HeadUiEffect::WifiDetails);
    case 108:{HeadUiAction action(HeadUiEffect::WifiKey);action.event=&event;return perform(action);}
    case 16:case 17:info.layout=8;info.response=event.command;info.input_limit=id==16?5:6;return screen(id==16?29:34,info);
    case 6:info.layout=8;info.input_kind=2;info.response=event.command;return screen(39,info);
    case 32:case 33:case 34:case 35:{const std::uint32_t programs[]={20,22,21,23};return effect(HeadUiEffect::CoffeeTweak,programs[id-32]);}
    case 36:case 37:case 38:case 39:{
        const std::uint32_t titles[]={5,20,42,43},ids[]={0,3,1,4};const Quantity values[]={numeric.parameters.water,numeric.parameters.intensity,numeric.parameters.temperature,numeric.parameters.time};
        info.layout=12;info.quantity_id=ids[id-36];info.quantity=values[id-36];return screen(titles[id-36],info);
    }
    case 95:numeric.manual=ManualStepParameters{};numeric.manual.byte_00=true;return effect(HeadUiEffect::ManualStepScreen);
    case 96:info.layout=11;info.selection=numeric.manual.location;return screen(45,info);
    case 97:case 98:case 99:{
        const Quantity values[]={numeric.manual.temperature,numeric.manual.hold,numeric.manual.drain};info.layout=12;info.quantity_id=id-92;info.quantity=values[id-97];return screen(id-55,info);
    }
    case 100:{numeric.manual.byte_00=false;CoreCommand command;command.id=static_cast<CoreCommandId>(54);command.manual=numeric.manual;return post(command);}
    case 101:numeric.manual.byte_00=false;return effect(HeadUiEffect::Synchronize);
    case 115:return effect(HeadUiEffect::UnitsConfiguration);
    case 116:case 117:return effect(HeadUiEffect::SetImperial,id==116);
    case 122:case 124:return effect(HeadUiEffect::SetStaticCycle,0);
    case 123:case 125:return effect(HeadUiEffect::SetStaticCycle,1);
    case 111:{static const std::uint32_t list[]={112,114,113,3};return menu(21,list,4);}
    case 112:case 113:case 114:return effect(HeadUiEffect::SetLanguage,id-112);
    case 119:case 120:return effect(HeadUiEffect::SetSlideshowDetail,id==119);
    case 126:return effect(HeadUiEffect::StillLoggingConfiguration);
    case 127:case 128:return effect(HeadUiEffect::SetStillLogging,id-126);
    case 27:HEAD_UI_TRY(effect(HeadUiEffect::ResetScreen));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,2));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,observed.t1_monitor?29:28));return effect(HeadUiEffect::InstructionMenu,observed.t1_monitor?80:79,31);
    case 129:return effect(HeadUiEffect::AzureConfiguration);
    case 130:case 131:return effect(HeadUiEffect::SetAzureMode,id==130?2:1);
    case 137:return effect(HeadUiEffect::BoilerConfiguration);
    case 12:{
        if(!(event.flags&4))HEAD_UI_TRY(assertion(0x1a8));
        numeric.parameters=ProgramParameters{};state.recipe=event.recipe;
        HeadUiAction action(HeadUiEffect::RecipeStrengthScreen);action.recipe=&event.recipe;return perform(action);
    }
    case 13:case 14:{
        const bool abv=id==13;if(abv)numeric.abv_pending=true;else numeric.ibu_pending=true;
        info.layout=10;info.strength=static_cast<std::uint32_t>(abv?numeric.parameters.abv:numeric.parameters.ibu);
        if(info.strength>10)info.strength=static_cast<std::uint32_t>(abv?state.recipe.parameters.abv:state.recipe.parameters.ibu);
        return screen(abv?22:23,info);
    }
    case 43:return effect(HeadUiEffect::Tests);
    case 44:return effect(HeadUiEffect::DeveloperTests);
    case 174:HEAD_UI_TRY(effect(HeadUiEffect::DisplayTest));state.display_suspended=true;return HeadUiResult::Ok;
    case 170:return effect(HeadUiEffect::MenuTest);
    case 171:{
        static const char* const labels[]={"Testing long menu entries on screen","For use of truncating and ellipsis","Short Line","6x8 edge case--1","6x8 edge case---2","Plus selection scrolling animation","Testing Testing 12345678910","For length with accented characters:","Short Line","ÀÁÂÃÄÅÆâàáãäåæÇçÈÉÊËèéêëÌÍÎÏÐìí"};
        HEAD_UI_TRY(effect(HeadUiEffect::ResetScreen));for(const char* label:labels){HeadUiAction action(HeadUiEffect::AppendCommandText);action.first=1;action.text=label;HEAD_UI_TRY(perform(action));}
        HeadUiAction action(HeadUiEffect::Menu);action.title="SCROLL MENU";action.first=10;action.second=1;return perform(action);
    }
    case 152:{
        static const std::uint32_t list[]={153,154,155,156,3};HEAD_UI_TRY(effect(HeadUiEffect::ResetScreen));for(auto command:list)HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,command));
        HeadUiAction action(HeadUiEffect::Menu);action.title="PAUSE TESTS";action.first=5;action.second=1;return perform(action);
    }
    case 153:case 154:case 155:case 156:return effect(HeadUiEffect::PauseTest);
    case 150:case 151:{HeadUiAction action(id==150?HeadUiEffect::InstructionsTest:HeadUiEffect::TitlesTest);action.event=&event;return perform(action);}
    case 169:return effect(HeadUiEffect::ScrollTest);
    case 175:{HeadUiAction action(HeadUiEffect::ParagraphTest);action.text="";return perform(action);}
    case 159:case 160:case 161:info.layout=8;info.input_kind=id==159?2:id==160?0:1;info.input_limit=id==160?5:0;return screen(id==159?"INPUT TEST":id==160?"NUM INPUT TEST":"RFID INPUT TST",info);
    case 162:info.layout=7;return screen("TEMP TEST",info);
    case 163:info.layout=9;info.argument=5;return screen("TIME TEST",info);
    case 164:info.layout=10;return screen("ABV LEVEL",info);
    case 165:info.layout=11;return screen("LOCATION",info);
    case 166:case 167:case 168:info.layout=12;info.quantity_id=id==166?0:id==167?3:1;info.quantity={id==166?2.0:id==167?5.0:150.0,static_cast<Unit>(id==166?1:id==167?8:5)};return screen(id==166?"BATCH SIZE":id==167?"INTENSITY":"TEMPERATURE",info);
    case 173:return effect(HeadUiEffect::AsciiFontTest);
    case 157:case 158:return effect(HeadUiEffect::SlideshowTest,id==157?6:5);
    case 172:HEAD_UI_TRY(effect(HeadUiEffect::ProgressTest));state.display_suspended=true;return HeadUiResult::Ok;
    case 132:return effect(HeadUiEffect::EepromScreen);
    case 134:{static const std::uint32_t list[]={133,3};return menu(46,list,2);}
    case 135:HEAD_UI_TRY(effect(HeadUiEffect::ResetScreen));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,3));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommand,136));return effect(HeadUiEffect::InstructionMenu,102,18);
    case 141:return effect(HeadUiEffect::HardwareScreen);
    case 83:{
        HEAD_UI_TRY(effect(HeadUiEffect::ResetRuntimeMenu));HEAD_UI_TRY(effect(HeadUiEffect::AppendCommandFront,1));
        HeadUiAction action(HeadUiEffect::Menu);action.second=1;
        const auto session=static_cast<std::uint32_t>(observed.session);
        action.title=observed.core==CoreStateId::Running&&observed.session_name_length&&session>=6&&session<=9?observed.session_name:ui_localized_text(UiTextCategory::SessionType,session,language);
        HEAD_UI_TRY(perform(action));if(!state.runtime_menu_duration)HEAD_UI_TRY(assertion(0x37));
        transient.active=true;transient.duration=state.runtime_menu_duration;if(!services.millis)return HeadUiResult::MissingService;
        transient.deadline=transient.duration+services.millis(services.context);return HeadUiResult::Ok;
    }
    case 50:case 51:return effect(HeadUiEffect::ConfirmStop,id==50?54:55);
    default:return effect(HeadUiEffect::Synchronize);
    }
}
#undef HEAD_UI_TRY
}} // namespace zseries::oem
