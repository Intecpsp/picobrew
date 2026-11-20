#include "zseries/hardware_console_tests.hpp"
#include "zseries/application_console.hpp"
#include "zseries/core_application.hpp"
#include "zseries/temperature.hpp"
#include <cstdio>
namespace zseries { namespace oem {
namespace {
struct TestScope {
    const ApplicationConsoleHooks& hooks;const char* tag;const char* file;std::uint32_t line;const char* name;bool active=false;
    TestScope(const ApplicationConsoleHooks& h,const char* t,const char* f,std::uint32_t l,const char* n):hooks(h),tag(t),file(f),line(l),name(n){}
    bool begin(){if(!hooks.test_scope)return false;active=hooks.test_scope(hooks.context,true,tag,file,line,name);return active;}
    bool end(){if(!active)return false;active=false;return hooks.test_scope(hooks.context,false,tag,file,line,name);}
    ~TestScope(){if(active)hooks.test_scope(hooks.context,false,tag,file,line,name);}
};
bool require(const ConsoleServices& s,bool value,const char* file,std::uint32_t line,const char* expression){return value||(s.assertion&&s.assertion(s.context,file,line,expression));}
bool enforce(const ConsoleServices& services,EepromData& data,bool& changed){
    return eeprom_enforce_valid_enums(data,changed,services)==EepromResult::Ok;
}
#define TEST_REQUIRE(value,file,line,expression) do{if(!require(services,(value),(file),(line),(expression)))return false;}while(false)
}
bool eeprom_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    {
        TestScope scope(hooks,"EPRM","Eeprom.h",0xa9,"EepromData::enforceAllEnumsAreValid()");if(!scope.begin())return false;
        EepromData data;bool changed;if(!enforce(services,data,changed))return false;
        TEST_REQUIRE(!changed,"Eeprom.h",0xac,"!data.enforceAllEnumsAreValid()");
        data.language=static_cast<LanguageType>(0xffffffffu);data.still_session_mode=static_cast<SessionMode>(0xffffffffu);data.azure_enabled=static_cast<BoolOption>(0xffffffffu);data.boiler=static_cast<BoilerType>(0xffffffffu);
        if(!enforce(services,data,changed))return false;
        TEST_REQUIRE(changed,"Eeprom.h",0xb4,"data.enforceAllEnumsAreValid()");if(!enforce(services,data,changed))return false;
        TEST_REQUIRE(!changed,"Eeprom.h",0xb7,"!data.enforceAllEnumsAreValid()");if(!scope.end())return false;
    }
    {
        TestScope scope(hooks,"EPRM","Eeprom.h",0xba,"EepromData::isValid()");if(!scope.begin())return false;
        EepromData data;TEST_REQUIRE(eeprom_data_valid(data),"Eeprom.h",0xbd,"data.isValid()");
        EepromBytes bytes;bytes.fill(0xff);data=decode_eeprom(bytes);TEST_REQUIRE(!eeprom_data_valid(data),"Eeprom.h",0xc0,"!data.isValid()");
        data.version=0;TEST_REQUIRE(!eeprom_data_valid(data),"Eeprom.h",0xc3,"!data.isValid()");
        data.wifi_ssid[0]=0;TEST_REQUIRE(!eeprom_data_valid(data),"Eeprom.h",0xc6,"!data.isValid()");
        data.wifi_password[0]=0;TEST_REQUIRE(!eeprom_data_valid(data),"Eeprom.h",0xc9,"!data.isValid()");
        data.boiler=BoilerType::Big;TEST_REQUIRE(eeprom_data_valid(data),"Eeprom.h",0xcc,"data.isValid()");
        data.boiler=BoilerType::Small;TEST_REQUIRE(eeprom_data_valid(data),"Eeprom.h",0xcf,"data.isValid()");
        data.boiler=BoilerType::Unknown;TEST_REQUIRE(!eeprom_data_valid(data),"Eeprom.h",0xd2,"!data.isValid()");
        data.boiler=static_cast<BoilerType>(9999);TEST_REQUIRE(!eeprom_data_valid(data),"Eeprom.h",0xd5,"!data.isValid()");if(!scope.end())return false;
    }
    {
        TestScope scope(hooks,"EPRM","Eeprom.h",0xd8,"EepromData::reset()");if(!scope.begin())return false;EepromData data;
        TEST_REQUIRE(data.boiler==BoilerType::Big,"Eeprom.h",0xda,"data.Boiler == BoilerType::Big");
        data.boiler=BoilerType::Small;data=EepromData{};data.boiler=BoilerType::Unknown;data=EepromData{};data.boiler=BoilerType::Big;if(!scope.end())return false;
    }
    {
        // This phase has no executable enum predicate between the original
        // ScopeLogger constructor and destructor; preserve that actual body.
        TestScope scope(hooks,"EPRM","Eeprom.h",0xe7,"EepromData::isEnumValid()");if(!scope.begin()||!scope.end())return false;
    }
    {
        TestScope scope(hooks,"EPRM","Eeprom.h",0xf2,"EepromData::isAzureEnabled()");if(!scope.begin())return false;EepromData data;
        TEST_REQUIRE(data.azure_enabled!=BoolOption::False,"Eeprom.h",0xf4,"data.isAzureEnabled()");if(!scope.end())return false;
    }
    return true;
}
bool max11613_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    {TestScope scope(hooks,"MAX11613","MAX11613.h",0x1bc,"CodeInvariant");if(!scope.begin()||!scope.end())return false;}
    TestScope scope(hooks,"MAX11613","MAX11613.h",0x1c2,"CanReadAllAdc");if(!scope.begin())return false;
    for(std::uint32_t index=0;index<4;++index){
        const auto type=static_cast<TempSensorType>(index);const auto* original=thermistor_parameters(type);if(!original)return false;const ThermistorParameters parameters=*original;
        TEST_REQUIRE(parameters.type==type,"MAX11613.h",0x1c9,"t == p.type");
        for(std::int32_t raw=15;raw<4095;++raw)(void)thermistor_kelvin(parameters,raw);
        for(const auto raw:{15,4094}){char message[128];const auto n=::snprintf(message,sizeof(message),"%u %s %d %3.1fC",static_cast<unsigned>(index),to_string(type),raw,thermistor_kelvin(parameters,raw)-273.15);
            if(n<0||static_cast<std::size_t>(n)>=sizeof(message)||!services.log||!services.log(services.context,ConsoleLevel::Info,"MAX11613","MAX11613.h",raw==15?0x1d2:0x1d6,message))return false;}
    }
    return scope.end();
}
bool hardware_console_test(std::uint32_t original_callback,CoreApplication&,const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    switch(original_callback){case 0x400dd678:return eeprom_console_self_test(services,hooks);case 0x400de068:return max11613_console_self_test(services,hooks);default:return false;}
}
#undef TEST_REQUIRE
}} // namespace zseries::oem
