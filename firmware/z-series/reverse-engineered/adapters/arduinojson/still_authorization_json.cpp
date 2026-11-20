#include "recipe_document.hpp"
#include "zseries/still_authorization.hpp"
namespace zseries { namespace oem {
std::size_t serialize_still_request_json(char* output,std::size_t capacity,const char* machine,const char* still,bool cleaned){
    if(!output||!capacity)return 0;
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;
    auto root=document.to<ArduinoJson::JsonObject>();root["MachineUID"]=machine;root["MachineType"]=2;
    root["PicoStillUID"]=still;root["HasCleanedAck"]=cleaned;
    const auto count=ArduinoJson::serializeJson(document,output,capacity-1);output[count]=0;return count;
}
RecipeJsonError parse_still_authorization_json(char* input,std::size_t length,StillAuthorizationJson& out){
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;
    const auto error=json_adapter::decode(document,input,length);if(error!=RecipeJsonError::Ok)return error;
    const auto value=document["PicoStill"];
    out.uid=value["UID"].as<const char*>();out.cleaned_ack_date=value["CleanedAckDate"].as<const char*>();out.profile_id=value["ProfileID"].as<std::int32_t>();
    return error;
}
}} // namespace zseries::oem
