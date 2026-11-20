#include "zseries/system_setup.hpp"
#include "zseries/diagnostics.hpp"
#include <cstring>
#include <vector>

using namespace zseries::oem;
namespace {
struct Context {
    const std::uint32_t* input;
    std::uint32_t now;
    DiagnosticState diagnostics;
    DiagnosticServices diagnostic_services;
    std::vector<std::uint32_t> events;
    unsigned missing = 0, failure = 99;
};
std::uint32_t clock(void* opaque) {
    auto& c = *static_cast<Context*>(opaque);
    c.events.push_back(20); c.events.push_back(c.now); return c.now;
}
void log_assertion(void* opaque, double seconds, const char* file,
                   std::uint32_t line, const char* expression) {
    auto& c = *static_cast<Context*>(opaque);
    std::uint64_t bits = 0; std::memcpy(&bits, &seconds, sizeof bits);
    c.events.insert(c.events.end(), {21,line,static_cast<std::uint32_t>(bits),
        static_cast<std::uint32_t>(bits >> 32),c.diagnostics.assertion_count});
    if (std::strcmp(file,"System.h") || std::strcmp(expression,"ESP_OK == ret"))
        c.events.push_back(0xbad);
}
void delay(void* opaque, std::uint32_t duration) {
    auto& c = *static_cast<Context*>(opaque);
    c.events.insert(c.events.end(), {22,duration,c.diagnostics.assertion_count}); c.now += duration;
}
bool assertion(void* opaque, std::uint32_t line, const char* expression) {
    auto& c = *static_cast<Context*>(opaque);
    return record_assertion(c.diagnostics,c.diagnostic_services,"System.h",line,expression)==DiagnosticResult::Ok;
}
bool perform(void* opaque,const SystemSetupAction& action,std::uintptr_t& result) {
    auto& c = *static_cast<Context*>(opaque);
    const auto kind=static_cast<std::uint32_t>(action.effect);
    c.events.push_back(kind);
    for(auto value:action.arguments)c.events.push_back(static_cast<std::uint32_t>(value));
    if(kind==c.failure)return false;
    result=0;
    switch(action.effect) {
    case SystemSetupEffect::BootPartition:result=c.input[4];break;
    case SystemSetupEffect::RunningPartition:result=c.input[5];break;
    case SystemSetupEffect::FactoryPartition:result=c.input[6];break;
    case SystemSetupEffect::WatchdogInitialize:result=c.input[2];break;
    case SystemSetupEffect::WatchdogAdd:result=c.input[3];break;
    default:break;
    }
    return true;
}
}
extern "C" unsigned system_setup_case(const std::uint32_t* input,unsigned missing,unsigned failure,
    std::uint32_t* output,std::uint32_t* events,unsigned* count) {
    Context c{};c.input=input;c.now=input[0];c.diagnostics.assertion_count=input[1];c.failure=failure;
    c.diagnostic_services.context=&c;
    c.diagnostic_services.millis=missing==3?nullptr:clock;
    c.diagnostic_services.assertion_log=missing==4?nullptr:log_assertion;
    c.diagnostic_services.delay_ms=missing==5?nullptr:delay;
    SystemSetupServices services;services.context=&c;
    services.perform=missing==1?nullptr:perform;services.assertion=missing==2?nullptr:assertion;
    SystemSetupState state;
    const auto result=setup_system(state,services);
    output[0]=c.now;output[1]=c.diagnostics.assertion_count;
    output[2]=static_cast<std::uint32_t>(state.boot_partition);
    output[3]=static_cast<std::uint32_t>(state.running_partition);
    output[4]=static_cast<std::uint32_t>(state.factory_partition);
    output[5]=static_cast<std::uint32_t>(state.watchdog_initialize_error);
    output[6]=static_cast<std::uint32_t>(state.watchdog_add_error);
    *count=static_cast<unsigned>(c.events.size());
    if(*count>256)return 99;
    std::memcpy(events,c.events.data(),c.events.size()*sizeof(std::uint32_t));
    return static_cast<unsigned>(result);
}
extern "C" const char* system_console_token(unsigned index) {
    return index<system_console_commands().size()?system_console_commands()[index].token:nullptr;
}
extern "C" const char* system_console_description(unsigned index) {
    return index<system_console_commands().size()?system_console_commands()[index].description:nullptr;
}
extern "C" unsigned system_console_exact(unsigned index) {
    return index<system_console_commands().size()&&system_console_commands()[index].exact_match;
}
