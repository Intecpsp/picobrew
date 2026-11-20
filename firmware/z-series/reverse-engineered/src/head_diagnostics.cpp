#include "zseries/head_application.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
namespace zseries { namespace oem { namespace {
struct Item {std::uint32_t command;const char* text;};
const Item TestsItems[]={
    {3,nullptr},
    {149,nullptr},
    {141,nullptr},
    {142,nullptr},
    {143,nullptr},
    {144,nullptr},
    {147,nullptr},
    {148,nullptr},
    {146,nullptr},
    {145,nullptr},
    {44,nullptr},
    {3,nullptr},
};
const Item DeveloperItems[]={
    {3,nullptr},
    {165,nullptr},
    {150,nullptr},
    {151,nullptr},
    {152,nullptr},
    {157,nullptr},
    {158,nullptr},
    {159,nullptr},
    {160,nullptr},
    {161,nullptr},
    {162,nullptr},
    {163,nullptr},
    {164,nullptr},
    {169,nullptr},
    {170,nullptr},
    {171,nullptr},
    {172,nullptr},
    {175,nullptr},
    {85,nullptr},
    {86,nullptr},
    {173,nullptr},
    {174,nullptr},
    {166,nullptr},
    {167,nullptr},
    {168,nullptr},
    {31,nullptr},
    {3,nullptr},
};
const Item MenuItems[]={
    {1,nullptr},
    {1,"01234567890123456789"},
    {1,"0123456789012345678"},
    {1,"012345678901234567"},
    {1,"0123456789012345"},
    {1,"012345678901234"},
    {1,"01234567890123"},
    {1,"0123456789012"},
    {1,"012345678901"},
    {1,"01234567890"},
    {1,"0123456789"},
    {1,"012345678"},
    {1,"01234567"},
    {1,"0123456"},
    {1,"012345"},
    {1,"01234"},
    {1,"0123"},
    {1,"012"},
    {1,"01"},
    {1,"0"},
    {1,""},
    {1,"Last"},
};
const Item PauseItems[]={
    {152,"Back"},
    {3,nullptr},
};
const char* const ScrollLines[]={
    "0   Scroll down to  0",
    "1 reveal more lines 1",
    "2         .         2",
    "3        . .        3",
    "4       .   .       4",
    "5      .     .      5",
    "6     .       .     6",
    "7    .         .    7",
    "8   .           .   8",
    "9  .             .  9",
    "10*               *10",
    "11 _             _ 11",
    "12  _           _  12",
    "13   _         _   13",
    "14    _       _    14",
    "15     _     _     15",
    "16      _   _      16",
    "17       _ _       17",
    "18        |        18",
    "19       | |       19",
    "20      |   |      20",
    "21     |     |     21",
    "22    |       |    22",
    "23   |         |   23",
    "24  |           |  24",
    "25 |             | 25",
    "26|____THE END____|26",
};
const char* const AsciiLines[]={
    "37\302\260C",
    "\303\200\303\201\303\202\303\203\303\204\303\205\303\206",
    "\303\242\303\240\303\241\303\243\303\244\303\245\303\246",
    "\303\207\303\247\303\210\303\211\303\212\303\213\303\250\303\251\303\252\303\253",
    "\303\214\303\215\303\216\303\217\303\220\303\254\303\255\303\256\303\257",
    "\302\251\302\256\302\266\302\262\302\263\302\277\302\253",
    "\302\261\302\264\302\265\302\254",
    "\303\222\303\223\303\224\303\225\303\226",
    "\303\262\303\260\303\263\303\264\303\265\303\266",
    "\303\261\303\221",
    "\303\231\303\232\303\233\303\234",
    "\303\271\303\272\303\273\303\274\303\275\303\276\303\277",
    "Out of range: ",
    "\312\244\312\246\324\261\357\273\246",
};
template<std::size_t N> bool items(HeadScreens& screens,const Item(&entries)[N]){screens.clear_data();for(const auto& entry:entries)if(!screens.menu_data.append(static_cast<UiCommand>(entry.command),entry.text&&*entry.text?entry.text:nullptr))return false;return true;}

}
bool HeadApplication::diagnostic_presentation(const HeadUiAction& action){
    switch(action.effect){
    case HeadUiEffect::Tests:if(!items(screens,TestsItems))return false;return screen_result(screens.menu_screen("TESTS",12,1));
    case HeadUiEffect::DeveloperTests:if(!items(screens,DeveloperItems))return false;return screen_result(screens.menu_screen("DEV TESTS",27,1));
    case HeadUiEffect::DisplayTest:if(!services.display_test){status.result=HeadApplicationResult::MissingBinding;return false;}return services.display_test(services.context);
    case HeadUiEffect::MenuTest:{if(!items(screens,MenuItems)||!screen_result(screens.menu_screen("",1,2)))return false;const auto& context=display_text_context;
        const char* lines[]={"Scroll to see select","other menu items.","Left","Center","Right"};for(unsigned i=0;i<5;++i){DisplayTextOptions options;options.row=i<2?i:3;options.alignment=static_cast<DisplayTextAlignment>(i<2?0:i-2);if(display_text(display,context,lines[i],std::strlen(lines[i])+1,options)!=DisplayTextResult::Ok)return false;}screens.painter.dirty=true;return true;}
    case HeadUiEffect::PauseTest:{const auto command=static_cast<std::uint32_t>(ui_events.value.command);if(command-153u>=4)return true;if(command<=154){if(!items(screens,PauseItems))return false;}else screens.clear_data();if(!screen_result(screens.reset())||!screen_result(screens.pause(static_cast<PauseReason>(command==153?4:command==155?7:2))))return false;events.state.encoder_press_hijacked=ui.state.display_suspended=true;return true;}
    case HeadUiEffect::InstructionsTest:case HeadUiEffect::TitlesTest:{
        if(!action.event)return false;
        if(!(action.event->flags&1)&&!assertion("ICore.h",0x1a6,"hasIndex()"))return false;
        const auto index=action.event->index,next=index+1;screens.clear_data();const bool instructions=action.effect==HeadUiEffect::InstructionsTest;
        if(static_cast<std::int32_t>(next)<(instructions?103:63)){HeadUiEvent event;event.command=static_cast<UiCommand>(instructions?150:151);event.flags=1;event.index=next;screens.menu_data.append(event,instructions?to_string(static_cast<ScreenId>(next)):ui_localized_text(UiTextCategory::TextTitle,next,screens.language));}screens.menu_data.append(UiCommand::Exit);
        const char* body="Foo";if(instructions&&ui_screen_text(static_cast<ScreenId>(index),screens.language,body)!=UiScreenTextResult::Ok)body="";
        HeadWrappedText wrapped;if(!head_wrap_text(wrapped,body))return false;std::array<const char*,12> lines{};for(std::uint32_t i=0;i<wrapped.count;++i)lines[i]=wrapped.lines[i].data();HeadScreenRequest request;request.screen.layout=17;request.title=instructions?to_string(static_cast<ScreenId>(index)):ui_localized_text(UiTextCategory::TextTitle,index,screens.language);request.has_body=true;request.body.lines=lines.data();request.body.count=wrapped.count;request.body.vertical=1;return screen_result(screens.show(request));}
    case HeadUiEffect::ScrollTest:case HeadUiEffect::AsciiFontTest:{screens.clear_data();const bool ascii=action.effect==HeadUiEffect::AsciiFontTest;if(ascii){for(auto line:AsciiLines)if(!screens.scroll_data.append(line))return false;}else{for(auto line:ScrollLines)if(!screens.scroll_data.append(line))return false;}HeadScreenRequest request;request.screen.layout=6;request.title=ascii?"ASCII TEST":"SCROLL TEST";request.custom_scroll=true;request.scroll_alignment=DisplayTextAlignment::Left;request.scroll_command=UiCommand::Back;return screen_result(screens.show(request));}
    case HeadUiEffect::ParagraphTest:return screen_result(screens.diagnostic_paragraph());
    case HeadUiEffect::SlideshowTest:{HeadScreenRequest request;request.screen.layout=4;request.screen.argument=action.first;request.screen.command=UiCommand::Back;return screen_result(screens.show(request));}
    case HeadUiEffect::ProgressTest:{
        if(!services.display_delay.milliseconds||!services.random_below){status.result=HeadApplicationResult::MissingBinding;return false;}HeadScreenRequest request;request.screen.layout=15;request.title="Progress:";if(!screen_result(screens.show(request)))return false;
        const auto flush=[&](){return display_flush(display,services.display_io)==DisplayResult::Ok;};const auto delay=[&](std::uint32_t time){services.display_delay.milliseconds(services.display_delay.context,time);};if(!flush())return false;delay(250);
        for(std::uint32_t i=2;i<=56;++i){if(!screen_result(screens.progress(i))||!flush())return false;delay(services.random_below(services.context,200));}delay(500);for(std::uint32_t i=57;i<67;i+=5){if(!screen_result(screens.progress(i))||!flush())return false;delay(300);}for(std::uint32_t i=67;i<=100;++i){if(!screen_result(screens.progress(i))||!flush())return false;delay(services.random_below(services.context,100));}return true;}
    case HeadUiEffect::EepromScreen:{
        if(!settings)return false;
        screens.clear_data();const auto boolean=[&](std::uint32_t value){return ui_localized_text(UiTextCategory::TextCommon,value^1,screens.language);};const char* values[]={settings->wifi_ssid.data(),settings->wifi_password[0]?"*":"",boolean(settings->use_imperial),to_string(settings->language),boolean(settings->show_detail),boolean(settings->slideshow_cycle_static^1),boolean(settings->use_ethernet),to_string(settings->still_session_mode)};
        for(std::uint32_t i=0;i<8;++i)if(!aligned_line(ui_localized_text(UiTextCategory::TextEeprom,i,screens.language),values[i]))return false;
        HeadScreenRequest request;request.screen.layout=6;request.title=ui_localized_text(UiTextCategory::TextTitle,14,screens.language);request.scroll_alignment=DisplayTextAlignment::Left;return screen_result(screens.show(request));}
    default:return false;
    }
}
HeadApplicationResult HeadApplication::console(HeadConsoleCommand command){
    status=HeadApplicationStatus{};if(!settings||!services.millis){status.result=HeadApplicationResult::MissingBinding;return status.result;}refresh_observed();bool ok=false;
    switch(command){
    case HeadConsoleCommand::Splash:ok=screen_result(screens.core_action(HeadCoreAction(HeadCoreEffect::BootLogo)));break;
    case HeadConsoleCommand::Hardware:ok=presentation(HeadUiEffect::HardwareScreen);break;
    case HeadConsoleCommand::Menu:ok=diagnostic_presentation(HeadUiAction(HeadUiEffect::Tests));break;
    case HeadConsoleCommand::UiMenu:ok=diagnostic_presentation(HeadUiAction(HeadUiEffect::DeveloperTests));break;
    case HeadConsoleCommand::Tests:
        if(!services.run_display_tests){status.result=HeadApplicationResult::MissingBinding;return status.result;}
        ok=screens.slideshow.self_test()==HeadWidgetResult::Ok&&services.run_display_tests(services.context);break;
    case HeadConsoleCommand::Slideshows:ok=screen_result(screens.reset())&&screens.slideshow.initialize_all()==HeadWidgetResult::Ok;break;
    }
    if(!ok&&status.result==HeadApplicationResult::Ok)status.result=HeadApplicationResult::ServiceFailure;
    return status.result;
}
}} // namespace zseries::oem
