#include "zseries/head_core_events.hpp"
#include <cstring>

namespace zseries { namespace oem {
#define HEAD_CORE_TRY(x) do { const auto result=(x); if(result!=HeadCoreResult::Ok)return result; } while(false)
HeadCoreResult HeadCoreEvents::perform(const HeadCoreAction& action,std::uint32_t* value) {
    if(!services.perform)return HeadCoreResult::MissingService;
    std::uint32_t result=0;
    if(!services.perform(services.context,action,result))return HeadCoreResult::ServiceFailure;
    if(value)*value=result;
    return HeadCoreResult::Ok;
}
HeadCoreResult HeadCoreEvents::effect(HeadCoreEffect kind,std::uint32_t first,std::uint32_t second,std::uint32_t third) {
    HeadCoreAction action(kind);action.first=first;action.second=second;action.third=third;return perform(action);
}
HeadCoreResult HeadCoreEvents::assertion(std::uint32_t line) {
    state.assertion_line=line;
    if(!services.assertion)return HeadCoreResult::MissingService;
    return services.assertion(services.context,line)?HeadCoreResult::Ok:HeadCoreResult::AssertionFailure;
}
HeadCoreResult HeadCoreEvents::waiting(std::uint32_t title,const char* detail,bool animated,std::uint32_t counter) {
    HeadCoreAction action(HeadCoreEffect::WaitingMessage);
    action.title=ui_localized_text(UiTextCategory::TextTitle,title,language);
    action.text=detail;action.first=animated;action.second=counter;return perform(action);
}
HeadCoreResult HeadCoreEvents::consume(CoreEventMailbox& mailbox) {
    mailbox.pending=false;
    const CoreEvent event=mailbox.value;
    return handle(event);
}
CoreStillEvent consume_head_still_event(PendingEvent<CoreStillEvent>& mailbox) noexcept {
    mailbox.pending=false;return mailbox.value;
}
HeadCoreResult HeadCoreEvents::transition(const CoreEvent& event) {
    switch(event.argument) {
    case 0:case 1:return effect(HeadCoreEffect::BootLogo);
    case 2:return waiting(12,observed.network_detail,true);
    case 3:return waiting(35,observed.network_detail,true);
    case 4:return waiting(12,nullptr,false,10);
    case 5:return effect(HeadCoreEffect::NetworkConfiguration);
    case 6:case 7:return waiting(51,nullptr,false);
    case 8:{
        std::int32_t percentage=0;
        if(observed.download_total>0) {
            const std::uint32_t bits=static_cast<std::uint32_t>(observed.download_received)*100u;
            std::int32_t numerator;std::memcpy(&numerator,&bits,sizeof numerator);
            percentage=numerator/observed.download_total;
        }
        const auto progress=static_cast<std::uint8_t>(percentage);
        if(progress>100)HEAD_CORE_TRY(assertion(0x102));
        HeadCoreAction action(HeadCoreEffect::ProgressScreen);
        action.title=ui_localized_text(UiTextCategory::TextTitle,54,language);
        action.first=progress;action.second=1;HEAD_CORE_TRY(perform(action));
        state.update_progress_visible=true;return HeadCoreResult::Ok;
    }
    case 9:{HeadCoreAction action(HeadCoreEffect::RegistrationScreen);action.text=observed.registration_token;return perform(action);}
    case 10:return waiting(13,nullptr,true);
    case 11:
        HEAD_CORE_TRY(effect(HeadCoreEffect::ResetUi));HEAD_CORE_TRY(effect(HeadCoreEffect::ClearDisplay));
        state.display_dirty=true;return HeadCoreResult::Ok;
    case 12:
        if(event.second_argument==11)HEAD_CORE_TRY(effect(HeadCoreEffect::BootLogo));
        else if(event.second_argument==7) {
            std::uint32_t screen=98,command=90;
            if(observed.interrupted_sessions<1) {
                screen=92;command=72;
                if(observed.maintenance_count<observed.maintenance_limit) {
                    HeadCoreAction action(HeadCoreEffect::CheckRinse);action.first=observed.maintenance_date;
                    std::uint32_t needed=0;HEAD_CORE_TRY(perform(action,&needed));
                    if(!needed)return effect(HeadCoreEffect::HomeScreen);
                    screen=91;command=71;
                }
            }
            HEAD_CORE_TRY(effect(HeadCoreEffect::ResetScreen));
            HEAD_CORE_TRY(effect(HeadCoreEffect::AppendCommand,command));
            HEAD_CORE_TRY(effect(HeadCoreEffect::AppendCommand,1));
            return effect(HeadCoreEffect::InstructionMenu,screen,19);
        }
        return effect(HeadCoreEffect::HomeScreen);
    case 13:return effect(HeadCoreEffect::SessionStartScreen);
    case 14:return effect(HeadCoreEffect::RunningScreen);
    case 15:return waiting(50,nullptr,true);
    case 16:return effect(HeadCoreEffect::SystemErrorScreen);
    default:return HeadCoreResult::Ok;
    }
}
HeadCoreResult HeadCoreEvents::handle(const CoreEvent& event) {
    switch(event.type) {
    case CoreEventType::StateTransition:return transition(event);
    case CoreEventType::RegistrationNewToken:{
        HeadCoreAction action(HeadCoreEffect::RegistrationScreen);action.text=observed.registration_token;return perform(action);
    }
    case CoreEventType::RegistrationTokenExpired:return effect(HeadCoreEffect::ExpiredRegistrationScreen,21,84,5);
    case CoreEventType::Loading:return waiting(24,nullptr,false);
    case CoreEventType::LoadingError:return effect(HeadCoreEffect::LoadingErrorScreen);
    case CoreEventType::SessionCreationError:{
        HEAD_CORE_TRY(effect(HeadCoreEffect::ResetScreen));
        HeadCoreAction action(HeadCoreEffect::AppendCommandText);action.first=52;
        action.text=ui_localized_text(UiTextCategory::TextCommon,10,language);HEAD_CORE_TRY(perform(action));
        action.first=49;action.text=ui_localized_text(UiTextCategory::TextCommon,20,language);HEAD_CORE_TRY(perform(action));
        return effect(HeadCoreEffect::InstructionMenu,58,16);
    }
    case CoreEventType::NetworkMissingError:return effect(HeadCoreEffect::NetworkErrorScreen);
    case CoreEventType::ProgramLoaded:case CoreEventType::ResumedProgramLoaded:{
        HeadCoreAction action(HeadCoreEffect::ProgramLoadedScreen);action.event=&event;return perform(action);
    }
    case CoreEventType::RecipeRefListLoaded:{HeadCoreAction action(HeadCoreEffect::RecipeListScreen);action.event=&event;return perform(action);}
    case CoreEventType::BoilerDetectResult:{HeadCoreAction action(HeadCoreEffect::BoilerResultScreen);action.event=&event;return perform(action);}
    case CoreEventType::RinseNeeded:case CoreEventType::CleanNeeded:{
        const bool rinse=event.type==CoreEventType::RinseNeeded;
        HEAD_CORE_TRY(effect(HeadCoreEffect::ResetScreen));
        HEAD_CORE_TRY(effect(HeadCoreEffect::AppendCommand,rinse?71:72));
        HEAD_CORE_TRY(effect(HeadCoreEffect::AppendCommand,1));
        return effect(HeadCoreEffect::InstructionMenu,rinse?91:92,19);
    }
    case CoreEventType::PowerButton:
        if(observed.runtime!=RuntimeStateId::Standby) {
            if(observed.runtime!=RuntimeStateId::Idle)return HeadCoreResult::Ok;
            if(!state.transient.active) {
                HEAD_CORE_TRY(effect(HeadCoreEffect::ResetScreen));
                HEAD_CORE_TRY(effect(HeadCoreEffect::AppendCommand,89));
                HEAD_CORE_TRY(effect(HeadCoreEffect::AppendCommand,1));
                HEAD_CORE_TRY(effect(HeadCoreEffect::InstructionMenu,93,32));
                if(!state.power_prompt_duration)HEAD_CORE_TRY(assertion(0x37));
                state.transient.active=true;state.transient.duration=state.power_prompt_duration;
                if(!services.millis)return HeadCoreResult::MissingService;
                state.transient.deadline=state.transient.duration+services.millis(services.context);
                return HeadCoreResult::Ok;
            }
        }
        return effect(HeadCoreEffect::DispatchCoreCommand,49);
    case CoreEventType::CoreError:{
        HEAD_CORE_TRY(effect(HeadCoreEffect::ResetScreen));
        HeadCoreAction line(HeadCoreEffect::AppendLine);line.text=to_string(event.error);HEAD_CORE_TRY(perform(line));
        HeadCoreAction screen(HeadCoreEffect::ErrorScrollScreen);
        screen.title=ui_localized_text(UiTextCategory::TextTitle,16,language);screen.first=6;screen.second=1;
        return perform(screen);
    }
    default:return HeadCoreResult::Ok;
    }
}
#undef HEAD_CORE_TRY
}} // namespace zseries::oem
