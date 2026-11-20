#include "zseries/head_remote_session.hpp"
#include <cstring>
namespace zseries { namespace oem {
HeadRemoteResult head_remote_session(const AzureCommand& incoming,std::uint32_t activity_date,std::uint32_t quiet_interval,const HeadRemoteServices& services){
    if(!services.millis)return HeadRemoteResult::MissingService;
    const auto assertion=[&](const char* file,std::uint32_t line,const char* expression){return !services.assertion?HeadRemoteResult::MissingService:services.assertion(services.context,file,line,expression)?HeadRemoteResult::Ok:HeadRemoteResult::ServiceFailure;};
#define REMOTE_TRY(expression) do{const auto result=(expression);if(result!=HeadRemoteResult::Ok)return result;}while(false)
    if(incoming.id!=CoreCommandId::AzureSessionLoad)REMOTE_TRY(assertion("Core.h",0x2ec,"ev.Id == CoreCommandId::AzureSessionLoad"));
    const auto program=static_cast<std::uint32_t>(incoming.session.program);
    if(!program){REMOTE_TRY(assertion("Core.h",0x2ed,"ev.RemoteSession.ProgramId != ZProgramId::None"));REMOTE_TRY(assertion("Core.h",0x2f0,"s.ProgramId != ZProgramId::None"));}
    const auto now=services.millis(services.context);
    if(now-activity_date>=quiet_interval){
        if(program<24&&(0xf1f07eu&(1u<<program))){
            HeadAzureEvent event;event.type=AzureEventType::DisplayStartProgram;event.program=incoming.session.program;
            if(program-20u<4u){double converted=0;if(!quantity_in(incoming.session.parameters.water,Unit::Liter,converted))REMOTE_TRY(assertion("Quantity.h",0x7d,"areCompatible(_unit, unit)"));event.parameters.water={converted,Unit::Liter};
                event.parameters.intensity={incoming.session.parameters.intensity.value,Unit::Intensity};event.parameters.time={incoming.session.parameters.time.value,Unit::Second};
                if(!quantity_in(incoming.session.parameters.temperature,Unit::Fahrenheit,converted))REMOTE_TRY(assertion("Quantity.h",0x7d,"areCompatible(_unit, unit)"));
                event.parameters.temperature={converted,Unit::Fahrenheit};}
            if(!services.post_azure)return HeadRemoteResult::MissingService;
            return services.post_azure(services.context,event)?HeadRemoteResult::Ok:HeadRemoteResult::ServiceFailure;
        }
        if(program==24){
            RecipeReference recipe;recipe.id=incoming.session.recipe_id;recipe.name=incoming.session.name;recipe.name_length=static_cast<std::uint32_t>(std::strlen(recipe.name.data()));
            CoreCommand command;const auto type=static_cast<std::uint32_t>(incoming.session.type);
            if(type==6){recipe.kind=static_cast<RecipeKind>(0);command.id=CoreCommandId::ZymaticRecipeLoad;}
            else if(type==7){recipe.kind=static_cast<RecipeKind>(1);command.id=CoreCommandId::PicoPakRecipeLoad;recipe.parameters.abv=incoming.session.parameters.abv;recipe.parameters.ibu=incoming.session.parameters.ibu;}
            else if(type==8){recipe.kind=static_cast<RecipeKind>(2);command.id=CoreCommandId::BrewPenRecipeLoad;}
            HeadUiCommandData data;data.flags=12;data.recipe=recipe;data.parameters=recipe.parameters;command.program_parameters=data.parameters;
            if(!services.post_core)return HeadRemoteResult::MissingService;
            return services.post_core(services.context,HeadUiCoreEnvelope(command,&data))?HeadRemoteResult::Ok:HeadRemoteResult::ServiceFailure;
        }
    }
    if(!services.discard)return HeadRemoteResult::MissingService;
    return services.discard(services.context)?HeadRemoteResult::Ok:HeadRemoteResult::ServiceFailure;
#undef REMOTE_TRY
}
}} // namespace zseries::oem
