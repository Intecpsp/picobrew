#include "recipe_document.hpp"
#include "zseries/azure.hpp"
namespace zseries { namespace oem {
BackendResult AzureService::receive(const char*,char* payload,std::size_t length){
    if(group_disabled)return diagnostic(0xeb);
    const auto core=static_cast<std::uint32_t>(core_state);
    if(state!=AzureState::Listening||(core-11u)>1)return diagnostic(0xf6);
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;
    json_error=json_adapter::decode(document,payload,length);
    if(json_error!=RecipeJsonError::Ok)return diagnostic(0xfe);
    const auto root=document.as<ArduinoJson::JsonVariantConst>();
    if(!root["Command"].isNull()){
        const auto kind=static_cast<std::uint32_t>(root["Command"].as<std::int32_t>());
        if(kind>2){auto result=assertion(0x107);if(result!=BackendResult::Ok)return result;}
        if(kind==2){auto result=assertion(0x124);if(result!=BackendResult::Ok)return result;return diagnostic(0x14a);}
        if(kind==1)return publish_state();
        if(kind!=0)return diagnostic(0x14a);
        AzureCommand command;command.recipe.id=root["ID"].as<std::int32_t>();
        const auto recipe_kind=static_cast<std::uint32_t>(root["Kind"].as<std::int32_t>());
        if(recipe_kind>3){auto result=assertion(0x12a);if(result!=BackendResult::Ok)return result;}
        command.recipe.kind=static_cast<RecipeKind>(recipe_kind);
        const auto name=root["Name"].as<const char*>();const auto size=name?std::strlen(name):0;
        command.recipe.name_length=static_cast<std::uint32_t>(std::min(size,command.recipe.name.size()-1));
        if(command.recipe.name_length)std::memcpy(command.recipe.name.data(),name,command.recipe.name_length);
        command.recipe.name_overflow=size>command.recipe.name_length;
        if(recipe_kind==0)command.id=static_cast<CoreCommandId>(6);
        else if(recipe_kind==2)command.id=static_cast<CoreCommandId>(8);
        else if(recipe_kind==1){command.id=static_cast<CoreCommandId>(9);command.recipe.parameters.abv=root["Abv"].as<std::int32_t>();command.recipe.parameters.ibu=root["Ibu"].as<std::int32_t>();}
        else return diagnostic(0x143);
        commands.put(command);return BackendResult::Ok;
    }
    if(root["SessionType"].isNull())return BackendResult::Ok;
    const auto version=root["FirmwareVersion"].as<const char*>();
    if(!version||std::strcmp(version,"0.0.119")!=0)return diagnostic(0x110);
    AzureCommand command;command.id=CoreCommandId::AzureSessionLoad;
    auto& session=command.session;const auto type=static_cast<std::uint32_t>(root["SessionType"].as<std::int32_t>());
    if(type>16){auto result=assertion(0xf);if(result!=BackendResult::Ok)return result;}
    session.type=static_cast<SessionType>(type);session.recipe_id=root["RecipeID"].as<std::int32_t>();
    const auto program_id=static_cast<std::uint32_t>(root["ZProgramId"].as<std::int32_t>());
    if(program_id>28){auto result=assertion(0xf);if(result!=BackendResult::Ok)return result;}
    session.program=static_cast<ZProgramId>(program_id);
    const auto name=root["Name"].as<const char*>();if(name)std::strncpy(session.name.data(),name,session.name.size()-1);
    if(program_id-20u<4||(program_id==24&&type==7)){
        if(root["ProgramParams"].isNull())return diagnostic(0x61);
        const auto parameters=root["ProgramParams"];
        if(program_id-20u<4){session.parameters.id=session.program;
            session.parameters.time={static_cast<double>(parameters["Duration"].as<std::int32_t>()),static_cast<Unit>(6)};
            session.parameters.water={json_adapter::number(parameters["Water"]),static_cast<Unit>(1)};
            session.parameters.intensity={json_adapter::number(parameters["Intensity"]),static_cast<Unit>(7)};
            session.parameters.temperature={static_cast<double>(parameters["Temperature"].as<std::int32_t>()),static_cast<Unit>(5)};
        }else{session.parameters.ibu=parameters["Ibu"].as<std::int32_t>();session.parameters.abv=parameters["Abv"].as<std::int32_t>();}
    }
    if(type>16)return diagnostic(0x32);
    if(program_id>28)return diagnostic(0x37);
    if(program_id==0)return diagnostic(0x3c);
    commands.put(command);return transition(AzureState::SessionLoaded);
}
}} // namespace zseries::oem
