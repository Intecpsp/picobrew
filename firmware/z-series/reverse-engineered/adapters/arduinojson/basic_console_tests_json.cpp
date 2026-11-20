#include "recipe_document.hpp"
#include "zseries/basic_console_json.hpp"
#include <cstring>
namespace zseries { namespace oem {
bool basic_console_decode_session_json(char*input,std::size_t length,AzureSession&session,RecipeJsonError&error){
    ArduinoJson::StaticJsonDocument<JSON_ARRAY_SIZE(32)> document;
    error=json_adapter::decode(document,input,length);if(error!=RecipeJsonError::Ok)return false;
    const auto root=document.as<ArduinoJson::JsonVariantConst>();
    const auto type=static_cast<std::uint32_t>(root["SessionType"].as<std::int32_t>());
    const auto program=static_cast<std::uint32_t>(root["ZProgramId"].as<std::int32_t>());
    session.type=static_cast<SessionType>(type);session.program=static_cast<ZProgramId>(program);
    session.recipe_id=root["RecipeID"].as<std::int32_t>();const char*name=root["Name"].as<const char*>();
    session.name.fill(0);if(name)std::strncpy(session.name.data(),name,session.name.size()-1);
    if(program-20u<4u||(program==24&&type==7)){
        const auto parameters=root["ProgramParams"];if(parameters.isNull())return false;
        if(program-20u<4u){session.parameters.id=session.program;
            session.parameters.time={static_cast<double>(parameters["Duration"].as<std::int32_t>()),Unit::Second};
            session.parameters.water={json_adapter::number(parameters["Water"]),Unit::Gallon};
            session.parameters.intensity={json_adapter::number(parameters["Intensity"]),Unit::Intensity};
            session.parameters.temperature={static_cast<double>(parameters["Temperature"].as<std::int32_t>()),Unit::Fahrenheit};
        }else{session.parameters.ibu=parameters["Ibu"].as<std::int32_t>();session.parameters.abv=parameters["Abv"].as<std::int32_t>();}
    }
    return type<=16&&program>0&&program<=28;
}
}} // namespace zseries::oem
