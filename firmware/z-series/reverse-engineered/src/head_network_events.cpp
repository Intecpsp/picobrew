#include "zseries/head_network_events.hpp"
namespace zseries { namespace oem {
#define HEAD_NET_TRY(expression) do{const auto result=(expression);if(result!=HeadNetworkResult::Ok)return result;}while(false)
HeadNetworkResult HeadNetworkEvents::perform(const HeadNetworkAction& action) {
    if(!services.perform)return HeadNetworkResult::MissingService;
    return services.perform(services.context,action)?HeadNetworkResult::Ok:HeadNetworkResult::ServiceFailure;
}
HeadNetworkResult HeadNetworkEvents::effect(HeadNetworkEffect effect,std::uint32_t first) {
    HeadNetworkAction action(effect);action.first=first;return perform(action);
}
HeadNetworkResult HeadNetworkEvents::wifi(const CoreWifiEvent& event) {
    if(static_cast<std::uint32_t>(core)-14u<3u)return HeadNetworkResult::Ok;
    if(event.type==WifiEventType::Error) {
        HEAD_NET_TRY(effect(HeadNetworkEffect::ResetScreen));
        HeadNetworkAction line(HeadNetworkEffect::AppendLine);line.text=to_string(event.type);HEAD_NET_TRY(perform(line));
        line.text=to_string(static_cast<WifiError>(event.argument));HEAD_NET_TRY(perform(line));
        HeadNetworkAction screen(HeadNetworkEffect::WifiErrorScreen);
        screen.title=ui_localized_text(UiTextCategory::TextTitle,0x38,language);screen.first=6;screen.second=1;
        return perform(screen);
    }
    if(event.type==WifiEventType::ScanStarted) {
        HeadNetworkAction action(HeadNetworkEffect::ScanningMessage);
        action.title=ui_localized_text(UiTextCategory::TextTitle,0x26,language);action.second=1;return perform(action);
    }
    if(event.type==WifiEventType::ScanCompleted&&core!=CoreStateId::ConnectingWiFi) {
        HeadNetworkAction action(HeadNetworkEffect::WifiScanResults);
        action.first=static_cast<std::uint32_t>(event.type);action.second=event.argument;action.third=event.second_argument;
        return perform(action);
    }
    return HeadNetworkResult::Ok;
}
HeadNetworkResult HeadNetworkEvents::group_session(const HeadAzureEvent& event) {
    HEAD_NET_TRY(effect(HeadNetworkEffect::ResetScreen));
    const std::uint32_t program=static_cast<std::uint32_t>(event.program);
    UiCommand start=UiCommand::None;
    switch(program) {
        case 1:start=UiCommand::StartRinse;break;
        case 2:start=UiCommand::StartDrain;break;
        case 3:start=UiCommand::StartRack;break;
        case 4:start=UiCommand::StartCirculate;break;
        case 27:start=UiCommand::StartChill;break;
        case 5:start=UiCommand::StartSousVideWithoutKeg;break;
        case 6:start=UiCommand::StartSousVideWithKeg;break;
        case 26:start=UiCommand::StartPicoStillBoilPoint;break;
        case 7:start=UiCommand::StartPicoStillManual;break;
        case 12:start=UiCommand::StartClean;break;
        case 13:start=UiCommand::StartPicoPaleDemo;break;
        case 14:start=UiCommand::StartValveTest;break;
        case 15:start=UiCommand::StartPumpsTest;break;
        case 16:start=UiCommand::StartAlarmTest;break;
        case 17:start=UiCommand::StartChillTest;break;
        default:break;
    }
    if(start!=UiCommand::None)HEAD_NET_TRY(effect(HeadNetworkEffect::AppendCommand,static_cast<std::uint32_t>(start)));
    const bool coffee=program-20u<4u;
    if(coffee) {
        HEAD_NET_TRY(effect(HeadNetworkEffect::AppendCommand,static_cast<std::uint32_t>(UiCommand::CoffeeProgramLoad)));
        parameters.id=event.program;
        double value;
        if(!quantity_in(event.parameters.water,Unit::Liter,value)){assertion_line=0x7d;return HeadNetworkResult::InvalidQuantity;}
        parameters.water={value,Unit::Liter};
        parameters.intensity={event.parameters.intensity.value,Unit::Intensity};
        parameters.time={event.parameters.time.value,Unit::Second};
        if(!quantity_in(event.parameters.temperature,Unit::Fahrenheit,value)){assertion_line=0x7d;return HeadNetworkResult::InvalidQuantity;}
        parameters.temperature={value,Unit::Fahrenheit};
    }else HEAD_NET_TRY(effect(HeadNetworkEffect::AppendCommand,static_cast<std::uint32_t>(UiCommand::GroupSessionExit)));
    HeadNetworkAction action(HeadNetworkEffect::GroupSessionScreen);
    action.title="Group Session";action.text=coffee?"Coffee Program loaded":"Start program?";
    action.first=2;action.second=1;action.third=1;return perform(action);
}
HeadNetworkResult HeadNetworkEvents::azure(const HeadAzureEvent& event) {
    if(event.type==AzureEventType::DisplayStartProgram)return group_session(event);
    if(event.type==AzureEventType::ExitStartProgram)
        HEAD_NET_TRY(effect(HeadNetworkEffect::PostCoreCommand,static_cast<std::uint32_t>(CoreCommandId::AzureSessionDiscard)));
    if(event.type==AzureEventType::ExitStartProgram||event.type==AzureEventType::TimeoutStartProgram)
        return effect(HeadNetworkEffect::HomeScreen);
    return HeadNetworkResult::Ok;
}
#undef HEAD_NET_TRY
}} // namespace zseries::oem
