#include "recipe_document.hpp"
#include "zseries/z_backend.hpp"
#include <cstring>

namespace zseries { namespace oem {
namespace {
std::size_t write_json(const ArduinoJson::JsonDocument& document,char* output,std::size_t capacity){
    if(!output||!capacity)return 0;
    output[0]=0;const auto size=ArduinoJson::serializeJson(document,output,capacity-1);
    output[size<capacity?size:capacity-1]=0;return size;
}
double converted_quantity(const Quantity& value,Unit target,const BackendHooks& hooks,BackendResult& status){
    if(static_cast<std::int32_t>(value.unit)>=8)return 0;
    double result=0;if(!quantity_in(value,target,result)){
        if(!hooks.assertion)status=BackendResult::MissingService;
        else if(!hooks.assertion(hooks.context,0x7d))status=BackendResult::ServiceFailure;
    }
    return result;
}
}
BackendSerialization serialize_session_json(char* output,std::size_t capacity,const SessionSnapshot& session,const BackendHooks& hooks){
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;
    BackendSerialization serialized;
    if(!output||!capacity){serialized.result=BackendResult::InvalidSpan;return serialized;}
    auto root=document.to<ArduinoJson::JsonObject>();
    root["SessionType"]=static_cast<std::int32_t>(session.type);
    root["RecipeID"]=static_cast<std::int32_t>(session.recipe_id);
    root["Name"]=session.name;root["FirmwareVersion"]="0.0.119";
    root["DurationSec"]=static_cast<std::int32_t>(session.stats.duration_seconds);
    root["PressurePa"]=session.stats.pressure_pascal;
    root["MaxTemp"]=session.stats.maximum_temperature_celsius;
    root["MaxTempAddedSec"]=static_cast<std::int32_t>(session.stats.maximum_temperature_added_seconds);
    root["ZProgramId"]=static_cast<std::int32_t>(session.program);root["GroupSession"]=session.group_session;
    auto parameters=root.createNestedObject("ProgramParams");
    parameters["Abv"]=session.parameters.abv;parameters["Ibu"]=session.parameters.ibu;
    parameters["Duration"]=converted_quantity(session.parameters.time,static_cast<Unit>(6),hooks,serialized.result);
    parameters["Water"]=converted_quantity(session.parameters.water,static_cast<Unit>(0),hooks,serialized.result);
    parameters["Intensity"]=converted_quantity(session.parameters.intensity,static_cast<Unit>(7),hooks,serialized.result);
    parameters["Temperature"]=converted_quantity(session.parameters.temperature,static_cast<Unit>(5),hooks,serialized.result);
    if(static_cast<std::uint32_t>(session.type)==8)root["RecipeGuid"]=session.recipe_guid;
    else if(static_cast<std::uint32_t>(session.type)==11){root["StillUID"]=session.still_uid;root["StillVer"]=session.still_version;}
    serialized.bytes=write_json(document,output,capacity);return serialized;
}
std::size_t serialize_session_log_json(char* output,std::size_t capacity,const SessionLogSnapshot& value,const HttpTiming& timing,const BackendHooks&){
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;
    auto root=document.to<ArduinoJson::JsonObject>();
    root["ZSessionID"]=static_cast<std::int32_t>(value.session_id);
    root["ThermoBlockTemp"]=value.thermoblock;root["WortTemp"]=value.wort;root["AmbientTemp"]=value.ambient;
    root["DrainTemp"]=value.drain;root["TargetTemp"]=value.target;root["StepName"]=value.step_name;
    root["ValvePosition"]=value.valve_position;root["KegPumpOn"]=value.keg_pump?1:0;root["DrainPumpOn"]=value.drain_pump?1:0;
    root["PauseReason"]=static_cast<std::int32_t>(value.pause_reason);root["ErrorCode"]=0;
    root["rssi"]=value.rssi;
    root["netSend"]=timing.send_duration;root["netWait"]=timing.reply_duration;root["netRecv"]=timing.body_duration;
    root["SecondsRemaining"]=value.seconds_remaining;
    return write_json(document,output,capacity);
}
RecipeJsonError parse_created_session_json(char* input,std::size_t length,std::uint32_t& id){
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;
    const auto error=json_adapter::decode(document,input,length);
    // The OEM still reads ID after a decode error, retaining a parsed prefix.
    id=document["ID"].as<std::int32_t>();return error;
}
}} // namespace zseries::oem
