#include "recipe_document.hpp"
#include "zseries/backend_console_tests.hpp"
#include "zseries/backend_log_event.hpp"
#include "zseries/backend_diagnostics.hpp"
#include "zseries/z_backend.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace zseries { namespace oem {
namespace {
bool log(const ConsoleServices& s,ConsoleLevel level,const char* tag,std::uint32_t line,const char* format,...){
    if(!s.log)return false;
    char output[1200];va_list args;va_start(args,format);const int length=::vsnprintf(output,sizeof output,format,args);va_end(args);
    return length>=0&&static_cast<std::size_t>(length)<sizeof output&&s.log(s.context,level,tag,"ZBackend.h",line,output);
}
bool require(const ConsoleServices& s,bool condition,std::uint32_t line,const char* expression){return condition||(s.assertion&&s.assertion(s.context,"ZBackend.h",line,expression));}
struct Scope {
    const ApplicationConsoleHooks& hooks;const char* name;std::uint32_t line;bool active=false;
    Scope(const ApplicationConsoleHooks& h,const char* n,std::uint32_t l):hooks(h),name(n),line(l){}
    bool begin(){active=hooks.test_scope&&hooks.test_scope(hooks.context,true,"ZBEND","ZBackend.h",line,name);return active;}
    bool end(){if(!active)return false;active=false;return hooks.test_scope(hooks.context,false,"ZBEND","ZBackend.h",line,name);}
    ~Scope(){if(active)(void)end();}
};
ConsoleResult event_output(BackendLogEvent& event,const ArduinoJson::JsonDocument& document,
        BackendLogEventType type,const ConsoleServices& services){
    event.json.fill(0);const auto length=ArduinoJson::serializeJson(document,event.json.data(),event.json.size()-1);
    event.json[length<event.json.size()?length:event.json.size()-1]=0;event.type=type;
    if(!services.log)return ConsoleResult::MissingService;
    return log(services,ConsoleLevel::Debug,"ZBEND",type==BackendLogEventType::MspError?0x52:0x47,
        type==BackendLogEventType::MspError?"MSP %s":"safety %s",event.json.data())?ConsoleResult::Ok:ConsoleResult::ServiceFailure;
}
#define TEST(value) do{if(!(value))return false;}while(false)
#define CHECK(value,line,expression) do{if(!require(services,(value),(line),(expression)))return false;}while(false)
}
ConsoleResult backend_log_msp_error(BackendLogEvent& event,std::uint32_t error,const ConsoleServices& services){
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;document["error"]=error;
    return event_output(event,document,BackendLogEventType::MspError,services);
}
ConsoleResult backend_log_safety_error(BackendLogEvent& event,const SafetyState& state,const ConsoleServices& services){
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;
    document["hasMaxHexWortDelta"]=state.max_hex_wort_delta;document["hasMaxWortDrainDelta"]=state.max_wort_drain_delta;
    document["hasMaxHexRise"]=state.max_hex_rise;document["isStepFilterMissing"]=state.filter_missing;
    document["tooHot"]=state.thermal.too_hot;document["tooCold"]=state.thermal.too_cold;document["tooOld"]=state.thermal.too_old;
    return event_output(event,document,BackendLogEventType::SafetyError,services);
}
bool z_backend_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    if(!services.assertion||!services.log||!hooks.test_scope)return false;
    Scope all(hooks,"AllUnitTest",0x18c);TEST(all.begin());
    ArduinoJson::StaticJsonDocument<json_adapter::local_capacity> document;
    {Scope scope(hooks,"testSession",0x191);TEST(scope.begin());document["SessionType"]=1;
        CHECK(document["SessionType"].as<int>()==static_cast<int>(SessionType::Clean),0x195,"st1 == (int)SessionType::Clean");
        document.clear();document["SessionType"]=static_cast<int>(SessionType::Clean);
        CHECK(document["SessionType"].as<int>()==static_cast<int>(SessionType::Clean),0x19a,"st2 == (int)SessionType::Clean");TEST(scope.end());}
    {Scope scope(hooks,"jsonTest1",0x19e);TEST(scope.begin());auto value=document.to<ArduinoJson::JsonObject>();
        for(unsigned stage=0;stage<2;++stage){
            if(stage)value.clear();
            const std::int32_t id=stage?987654321:1234;const float block=stage?100.9f:.1f;
            const char* step=stage?"testStepNameWithReallyLongString...................................":"testStepName";
            value["ZSessionID"]=id;value["ThermoBlockTemp"]=block;value["WortTemp"]=block;
            value["AmbientTemp"]=23.1f;value["DrainTemp"]=50.1f;value["TargetTemp"]=100.0f;
            value["ValvePosition"]=1;value["KegPumpOn"]=stage?0:1;value["DrainPumpOn"]=stage?0:1;
            value["StepName"]=step;value["PauseReason"]=stage?3:0;value["ErrorCode"]=stage?987654321:0;
            const auto log_id=value["ZSessionID"].as<std::int32_t>();const float thermoblock=value["ThermoBlockTemp"].as<float>(),wort=value["WortTemp"].as<float>();
            const float ambient=value["AmbientTemp"].as<float>(),drain=value["DrainTemp"].as<float>(),target=value["TargetTemp"].as<float>();
            const int valve=value["ValvePosition"].as<int>(),keg=value["KegPumpOn"].as<int>(),drain_pump=value["DrainPumpOn"].as<int>();
            const char* step_name=value["StepName"].as<const char*>();const auto pause=value["PauseReason"].as<int>(),error=value["ErrorCode"].as<std::int32_t>();
            if(!stage){TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1bc,"blockTemp1 = %f",static_cast<double>(thermoblock)));
                TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1bd,"wortTemp = %f",static_cast<double>(wort)));
                TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1be,"ambientTemp1 = %f",static_cast<double>(ambient)));
                TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1bf,"drainTemp1 = %f",static_cast<double>(drain)));
                TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1c0,"targetTemp1 = %f",static_cast<double>(target)));
                // OEM passes int to %f here, reading an unrelated stack word.
                // Render the checked integer without reproducing undefined ABI reads.
                TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1c1,"valvePosition1 = %d",valve));
                CHECK(log_id==1234,0x1c2,"logId1 == 1234");CHECK(thermoblock==.1f,0x1c3,"blockTemp1 == .1f");CHECK(wort==.1f,0x1c4,"wortTemp1 == .1f");
                CHECK(ambient==23.1f,0x1c5,"ambientTemp1 == 23.1f");CHECK(drain==50.1f,0x1c6,"drainTemp1 == 50.1f");CHECK(target==100.0f,0x1c7,"targetTemp1 == 100.0f");
                CHECK(valve==1,0x1c8,"valvePosition1 == 1");CHECK(keg==1,0x1c9,"kegPumpOn1 == 1");CHECK(drain_pump==1,0x1ca,"drainPumpOn1 == 1");
                CHECK(step_name&&std::strcmp(step_name,"testStepName")==0,0x1cb,"CString::areEquals(stepName1, \"testStepName\")");
                CHECK(error==0,0x1cc,"errorCode1 == 0");CHECK(pause==0,0x1cd,"pauseReason1 == PauseReason::None");
            }else{TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1ea,"blockTemp2 = %f",static_cast<double>(thermoblock)));
                TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1eb,"wortTemp2 = %f",static_cast<double>(wort)));
                TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1ec,"ambientTemp2 = %f",static_cast<double>(ambient)));
                TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1ed,"drainTemp2 = %f",static_cast<double>(drain)));
                TEST(log(services,ConsoleLevel::Debug,"ZBACKEND",0x1ee,"valvePosition2 = %d",valve));
                CHECK(log_id==987654321,0x1ef,"logId2 == 987654321");CHECK(thermoblock==100.9f,0x1f0,"blockTemp2 == 100.9f");CHECK(wort==100.9f,0x1f1,"wortTemp2 == 100.9f");
                CHECK(ambient==23.1f,0x1f2,"ambientTemp2 == 23.1f");CHECK(drain==50.1f,0x1f3,"drainTemp2 == 50.1f");CHECK(target==100.0f,0x1f4,"targetTemp2 == 100.0f");
                CHECK(valve==1,0x1f5,"valvePosition2 == 1");CHECK(keg==0,0x1f6,"kegPumpOn2 == 0");CHECK(drain_pump==0,0x1f7,"drainPumpOn2 == 0");
                CHECK(step_name&&std::strcmp(step_name,step)==0,0x1f8,"CString::areEquals(stepName2, \"testStepNameWithReallyLongString...................................\")");
                CHECK(error==987654321,0x1f9,"errorCode2 == 987654321");CHECK(pause==3,0x1fa,"pauseReason2 == PauseReason::StepFilter");}
        }TEST(scope.end());}
    {Scope scope(hooks,"jsonTest2",0x1fe);TEST(scope.begin());document.clear();
        char input[]="{\"ID\": 123456789,\"ZSeriesID\": 123456789,\"SessionType\": 1,\"Name\": \"some name\",\"RecipeID\": 123,\"CreationDate\": \"2018-08-07T16:39:45.753\",\"ClosingDate\": null,\"GUID\": \"95736E8944B74556AA3DEB03A4238988\",\"Deleted\": false,\"Notes\": null,\"Lat\": 47.54557,\"Lng\": -122.298431,\"CityLat\": 47.606209,\"CityLng\": -122.332069,\"Active\": true,\"SessionLogs\": []}";
        CHECK(json_adapter::decode(document,input,std::strlen(input))==RecipeJsonError::Ok,0x215,"DeserializationError::Ok == err");TEST(scope.end());}
    const char* names[]={"printZSessionToTest1","printZSessionToTest2","printZSessionToTest3"};
    const std::uint32_t lines[]={0x21a,0x224,0x22e},print_lines[]={0x221,0x22b,0x235};
    const char* recipe_names[]={"Recipe 12345","","Zymatic recipe with a long name"};const std::uint32_t ids[]={12345,0xffffffffu,543210};
    for(unsigned i=0;i<3;++i){Scope scope(hooks,names[i],lines[i]);TEST(scope.begin());SessionSnapshot session;session.group_session=true;session.type=static_cast<SessionType>(i==2?6:0);session.recipe_id=ids[i];session.name=recipe_names[i];
        BackendHooks backend_hooks;backend_hooks.context=const_cast<ConsoleServices*>(&services);backend_hooks.assertion=[](void* p,std::uint32_t line){return report_backend_assertion(*static_cast<ConsoleServices*>(p),BackendDiagnosticDomain::Session,line);};
        char output[1024];const auto serialized=serialize_session_json(output,sizeof output,session,backend_hooks);TEST(serialized.result==BackendResult::Ok);
        TEST(log(services,ConsoleLevel::Debug,"TEST",print_lines[i],"%s",output));TEST(scope.end());}
    BackendLogEvent event;TEST(backend_log_msp_error(event,0,services)==ConsoleResult::Ok);CHECK(event.type==BackendLogEventType::MspError,0x28e,"logEv.EventType == LogEventType::MSPError");
    SafetyState safety;TEST(backend_log_safety_error(event,safety,services)==ConsoleResult::Ok);CHECK(event.type==BackendLogEventType::SafetyError,0x291,"logEv.EventType == LogEventType::SafetyError");
    return all.end();
}
#undef TEST
#undef CHECK
}} // namespace zseries::oem
