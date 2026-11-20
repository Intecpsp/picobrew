#include "zseries/software_console_tests.hpp"
#include "zseries/core_application.hpp"
#include "zseries/head_dynamic.hpp"
#include "zseries/program_repository.hpp"
#include "zseries/serial_logger.hpp"
#include "zseries/still_authorization.hpp"
#include "zseries/still_client.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace zseries { namespace oem {
namespace {
struct Scope {
    const ApplicationConsoleHooks& hooks;const char* tag;const char* file;std::uint32_t line;const char* name;bool active=false;
    Scope(const ApplicationConsoleHooks& h,const char* t,const char* f,std::uint32_t l,const char* n):hooks(h),tag(t),file(f),line(l),name(n){}
    bool begin(){if(!hooks.test_scope)return false;active=hooks.test_scope(hooks.context,true,tag,file,line,name);return active;}
    bool end(){if(!active)return false;active=false;return hooks.test_scope(hooks.context,false,tag,file,line,name);}
    ~Scope(){if(active)hooks.test_scope(hooks.context,false,tag,file,line,name);}
};
bool require(const ConsoleServices& s,bool value,const char* file,std::uint32_t line,const char* expression){return value||(s.assertion&&s.assertion(s.context,file,line,expression));}
bool log(const ConsoleServices& s,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* pattern,...){
    if(!s.log)return false;
    char text[768];va_list args;va_start(args,pattern);const auto n=::vsnprintf(text,sizeof(text),pattern,args);va_end(args);
    return n>=0&&static_cast<std::size_t>(n)<sizeof(text)&&s.log(s.context,level,tag,file,line,text);
}
const char* truth(bool value){return value?"True":"False";}
bool instruction_out(const ConsoleServices& services,const Instruction& instruction){
    char argument[32],op[48];if(!head_instruction_argument_text(argument,sizeof(argument),instruction))return false;
    ::snprintf(op,sizeof(op),"OpCode::%s,",to_string(instruction.opcode));
    return log(services,ConsoleLevel::Info,"PGM","RuntimeState.h",0x332,"{ %-22s 0x%04x }, // %s",op,instruction.argument,argument);
}
bool stack_out(const ConsoleServices& services,const SoftwareConsoleClock& clock,const InstructionStack& stack){
    if(!clock.millis)return false;
    const auto begin=clock.millis(clock.context);
    if(!log(services,ConsoleLevel::Info,"STK","Runtime.h",0x7c,"out BEGIN 0")||
       !log(services,ConsoleLevel::Info,"STK",nullptr,0,"codeSize = %u",static_cast<unsigned>(stack.size())))return false;
    for(std::size_t i=0;i<stack.size();++i)if(!instruction_out(services,stack.at(i)))return false;
    return log(services,ConsoleLevel::Info,"STK","Runtime.h",0x7c,"out END   %u",clock.millis(clock.context)-begin);
}
bool drop_out(const ConsoleServices& services,const TemperatureDropWatch& watch){
    return log(services,ConsoleLevel::Info,"DMON","RuntimeState.h",0x3bc,"TEST %s  [dropped = %s] [max = %5.1f]",watch.enabled?"ACTIVE":"INACTIVE",truth(watch.triggered),watch.maximum);
}
bool sensors_out(const ConsoleServices& services,const PicoStillSensors& sample){
    if(!log(services,ConsoleLevel::Info,"STILS","IPicoStill.h",0xc6,"Timestamp = %u",sample.timestamp))return false;
    for(unsigned i=0;i<4;++i)if(!log(services,ConsoleLevel::Info,"STILS","IPicoStill.h",0xc7+i,"T%u = %.1f (%.1f )",i+1,sample.temperature[i],sample.speed[i]))return false;
    return log(services,ConsoleLevel::Info,"STILS","IPicoStill.h",0xcb,"Psi = %.1f (%.1f) ",sample.pressure,sample.speed[4])&&
        log(services,ConsoleLevel::Info,"STILS","IPicoStill.h",0xcc,"IsPumpRunning = %s",truth(sample.pump_running))&&
        log(services,ConsoleLevel::Info,"STILS","IPicoStill.h",0xcd,"IsFanRunning = %s",truth(sample.fan_running))&&
        log(services,ConsoleLevel::Info,"STILS","IPicoStill.h",0xce,"IsProgramRunning = %s",truth(sample.program_running))&&
        log(services,ConsoleLevel::Info,"STILS","IPicoStill.h",0xcf,"Error = %s",sample.error.data());
}
bool parse_sample(PicoStillSamples& sample,const char* text,const ConsoleServices& services,bool& accepted){
    accepted=sample.parse(1234,text,std::strlen(text),&services);
    return sample.diagnostics_result==ConsoleResult::Ok;
}
bool authorization_out(const ConsoleServices& services,const StillAuthorizationRecord& state){
    return log(services,ConsoleLevel::Info,"BSTIL",nullptr,0,"UID = %s",state.uid.data())&&
        log(services,ConsoleLevel::Info,"BSTIL",nullptr,0,"ProfileID = %d",state.profile_id)&&
        log(services,ConsoleLevel::Info,"BSTIL",nullptr,0,"CleanedAckDate = %s",state.cleaned_ack_date.data())&&
        log(services,ConsoleLevel::Info,"BSTIL",nullptr,0,"hasCleanedAckDate = %s",truth(state.cleaned()));
}
bool parse_authorization(StillAuthorization& backend,const char* json,const ConsoleServices& services,bool& accepted){
    if(!log(services,ConsoleLevel::Info,"BSTIL",nullptr,0,"parseJson = %s",json))return false;
    backend.reset_buffer();const auto size=std::strlen(json);if(size>512)return false;
    std::memcpy(backend.buffer.data(),json,size+1);backend.length=size;backend.remaining=512-size;
    if(backend.parse()!=StillAuthorizationResult::Ok)return false;
    accepted=backend.json_error==RecipeJsonError::Ok;return true;
}
#define TEST_REQUIRE(value,file,line,expression) do{if(!require(services,(value),(file),(line),(expression)))return false;}while(false)
#define TEST_TRY(value) do{if(!(value))return false;}while(false)
}
bool runtime_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks,const SoftwareConsoleClock& clock){
    if(!clock.millis)return false;
    TEST_TRY(log(services,ConsoleLevel::Info,"STK",nullptr,0,"MaxCodeSize = %u",static_cast<unsigned>(InstructionStack::capacity)));
    TEST_TRY(log(services,ConsoleLevel::Info,"STK",nullptr,0,"sizeof(code) = %u",static_cast<unsigned>(InstructionStack::capacity*sizeof(Instruction))));
    InstructionStack stack;
    {Scope scope(hooks,"RUNT","Runtime.h",0x8d,"Stack::isEmpty()");TEST_TRY(scope.begin());TEST_REQUIRE(stack.size()==0,"Runtime.h",0x8e,"s.isEmpty()");TEST_TRY(scope.end());}
    {Scope scope(hooks,"RUNT","Runtime.h",0x94,"Stack::push()");TEST_TRY(scope.begin());
        const auto& program=builtin_programs()[2].view;const auto begin=clock.millis(clock.context);
        TEST_TRY(log(services,ConsoleLevel::Info,"STK","Runtime.h",0x55,"push BEGIN 0"));TEST_TRY(stack.load(program)==RuntimeResult::Ok);
        TEST_TRY(log(services,ConsoleLevel::Info,"STK","Runtime.h",0x55,"push END   %u",clock.millis(clock.context)-begin));
        TEST_REQUIRE(program.code_size==stack.size(),"Runtime.h",0x96,"p.getCodeSize() == s.getSize()");TEST_REQUIRE(stack.size()!=0,"Runtime.h",0x97,"!s.isEmpty()");TEST_TRY(scope.end());}
    {Scope scope(hooks,"RUNT","Runtime.h",0x9b,"Stack::out()");TEST_TRY(scope.begin());TEST_TRY(stack_out(services,clock,stack));TEST_TRY(scope.end());}
    {Scope scope(hooks,"RUNT","Runtime.h",0xa0,"Stack::reset()");TEST_TRY(scope.begin());stack.clear();
        TEST_REQUIRE(stack.size()==0,"Runtime.h",0xa2,"0 == s.getSize()");TEST_REQUIRE(stack.size()==0,"Runtime.h",0xa3,"s.isEmpty()");TEST_TRY(scope.end());}
    TEST_TRY(stack_out(services,clock,stack));
    {Scope scope(hooks,"RUNT","Runtime.h",0xa9,"Stack::pop()");TEST_TRY(scope.begin());
        // The original has reset this stack, so its genuine loop executes zero times.
        while(stack.size()){Instruction instruction;TEST_TRY(stack.pop(instruction)==RuntimeResult::Ok);TEST_TRY(instruction_out(services,instruction));}TEST_TRY(scope.end());}
    {Scope scope(hooks,"DMON","Runtime.h",0x108,"DropMonitor");TEST_TRY(scope.begin());ProgramRuntime runtime;runtime.services.diagnostics=services;auto& watch=runtime.state.t1_drop_watch;
        TEST_TRY(drop_out(services,watch));TEST_REQUIRE(!watch.enabled,"Runtime.h",0x10e,"!m.isStarted()");TEST_REQUIRE(!watch.triggered,"Runtime.h",0x10f,"!m.hasDropped()");
        TEST_TRY(runtime.start_t1_drop_watch(2)==RuntimeResult::Ok);
        TEST_REQUIRE(watch.enabled,"Runtime.h",0x112,"m.isStarted()");TEST_REQUIRE(!watch.triggered,"Runtime.h",0x113,"!m.hasDropped()");
        TEST_TRY(runtime.update_t1_drop_watch()==RuntimeResult::Ok);TEST_REQUIRE(watch.enabled,"Runtime.h",0x116,"m.isStarted()");TEST_REQUIRE(!watch.triggered,"Runtime.h",0x117,"!m.hasDropped()");
        runtime.observed.scalar_958=10;TEST_TRY(runtime.update_t1_drop_watch()==RuntimeResult::Ok);
        TEST_REQUIRE(watch.enabled,"Runtime.h",0x11b,"m.isStarted()");TEST_REQUIRE(!watch.triggered,"Runtime.h",0x11c,"!m.hasDropped()");TEST_REQUIRE(watch.maximum==10,"Runtime.h",0x11d,"10.0 == m.getMax()");
        runtime.observed.scalar_958=9;TEST_TRY(runtime.update_t1_drop_watch()==RuntimeResult::Ok);TEST_REQUIRE(watch.enabled,"Runtime.h",0x121,"m.isStarted()");TEST_REQUIRE(!watch.triggered,"Runtime.h",0x122,"!m.hasDropped()");TEST_REQUIRE(watch.maximum==10,"Runtime.h",0x123,"10.0 == m.getMax()");
        runtime.observed.scalar_958=7;TEST_TRY(runtime.update_t1_drop_watch()==RuntimeResult::Ok);
        TEST_REQUIRE(watch.enabled,"Runtime.h",0x127,"m.isStarted()");TEST_REQUIRE(watch.triggered,"Runtime.h",0x128,"m.hasDropped()");TEST_REQUIRE(watch.maximum==10,"Runtime.h",0x129,"10.0 == m.getMax()");TEST_TRY(drop_out(services,watch));TEST_TRY(scope.end());}
    {Scope scope(hooks,"RUNT","Runtime.h",0x68f,"ValveLocationCastToInt");TEST_TRY(scope.begin());
        const ValveLocation values[]={ValveLocation::Passthrough,ValveLocation::Mash,ValveLocation::Adjunct1,ValveLocation::Adjunct2,ValveLocation::Adjunct3,ValveLocation::Adjunct4};
        const char* expressions[]={"VALVE_PASSTHROUGH == castEnumToInt(ValveLocation::Passthrough)","VALVE_MASH == castEnumToInt(ValveLocation::Mash)","VALVE_ADJUNCT1 == castEnumToInt(ValveLocation::Adjunct1)","VALVE_ADJUNCT2 == castEnumToInt(ValveLocation::Adjunct2)","VALVE_ADJUNCT3 == castEnumToInt(ValveLocation::Adjunct3)","VALVE_ADJUNCT4 == castEnumToInt(ValveLocation::Adjunct4)"};
        for(unsigned i=0;i<6;++i){TEST_REQUIRE(static_cast<unsigned>(values[i])==i+1,"Runtime.h",0x691+i,expressions[i]);}
        TEST_TRY(scope.end());}
    return true;
}
bool still_sensors_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    {Scope scope(hooks,"STILS","IPicoStill.h",0x100,"PicoStillSensors::hasData()");TEST_TRY(scope.begin());PicoStillSensors a;
        TEST_REQUIRE(!a.has_data(),"IPicoStill.h",0x102,"!a.hasData()");TEST_REQUIRE(!a.has_speed_data(),"IPicoStill.h",0x103,"!a.hasSpeedData()");
        a.timestamp=1;TEST_REQUIRE(!a.has_speed_data(),"IPicoStill.h",0x106,"!a.hasSpeedData()");a.delta_ms=1;TEST_REQUIRE(a.has_speed_data(),"IPicoStill.h",0x108,"a.hasSpeedData()");TEST_TRY(scope.end());}
    Scope scope(hooks,"STILS","IPicoStill.h",0x10b,"PicoStillSensors::updateSpeeds()");TEST_TRY(scope.begin());PicoStillSensors a,b;a.timestamp=b.timestamp=1234;b.temperature[0]=10;b.update_speeds(a);
    TEST_REQUIRE(!a.has_speed_data(),"IPicoStill.h",0x115,"!a.hasSpeedData()");b.timestamp=a.timestamp+60000;b.update_speeds(a);
    TEST_REQUIRE(b.has_speed_data(),"IPicoStill.h",0x11a,"b.hasSpeedData()");TEST_REQUIRE(b.delta_ms==60000,"IPicoStill.h",0x11b,"b.dt == 60000");TEST_REQUIRE(b.speed[0]==10,"IPicoStill.h",0x11c,"b.T1Speed == 10.0");
    a.timestamp=0xffffffffu;b.timestamp=59999;b.update_speeds(a);
    TEST_REQUIRE(b.has_speed_data(),"IPicoStill.h",0x122,"b.hasSpeedData()");TEST_REQUIRE(b.delta_ms==60000,"IPicoStill.h",0x123,"b.dt == 60000");TEST_REQUIRE(b.speed[0]==10,"IPicoStill.h",0x124,"b.T1Speed == 10.0");TEST_TRY(sensors_out(services,b));return scope.end();
}
bool still_state_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    {Scope scope(hooks,"STILS","IPicoStill.h",0x1ca,"PicoStillState::outCsvTo()");TEST_TRY(scope.begin());PicoStillSamples sample;CsvBuffer buffer;SerialLogServices sink;
        sink.context=services.context;sink.assertion=services.assertion;
        TEST_TRY(buffer.append("t1,t2,t3,t4,p,pdev,t1dev,t2dev",sink)==SerialLogResult::Ok);
        TEST_REQUIRE(buffer.length!=0,"IPicoStill.h",0x1ce,"!buf.isEmpty()");TEST_REQUIRE(buffer.text[0]!=0,"IPicoStill.h",0x1cf,"!CString::isNullOrEmpty(buf.c_ptr())");buffer.clear();
        TEST_TRY(still_csv_data(sample,buffer,sink)==SerialLogResult::Ok);TEST_REQUIRE(buffer.length!=0,"IPicoStill.h",0x1d4,"!buf.isEmpty()");TEST_REQUIRE(buffer.text[0]!=0,"IPicoStill.h",0x1d5,"!CString::isNullOrEmpty(buf.c_ptr())");TEST_TRY(scope.end());}
    {Scope scope(hooks,"STILS","IPicoStill.h",0x1d8,"PicoStillState::hasError()");TEST_TRY(scope.begin());PicoStillState state;
        TEST_REQUIRE(!still_has_error(state),"IPicoStill.h",0x1da,"!s.hasError()");state.polling_stopped=true;std::uint32_t line=0;const char* text=still_error_text(state,line);
        TEST_REQUIRE(text&&text[0],"IPicoStill.h",0x1dd,"!CString::isNullOrEmpty(s.getErrorText())");TEST_TRY(scope.end());}
    Scope scope(hooks,"STILS","IPicoStill.h",0x1e0,"PicoStillState::updateStats()");TEST_TRY(scope.begin());PicoStillSamples sample;sample.current.timestamp=1;sample.update_statistics(1);
    TEST_REQUIRE(sample.current.has_data(),"IPicoStill.h",0x1e6,"s.Sensors.hasData()");TEST_REQUIRE(sample.seconds10.has_data(),"IPicoStill.h",0x1e7,"s.Sensors10.hasData()");TEST_REQUIRE(sample.seconds60.has_data(),"IPicoStill.h",0x1e8,"s.Sensors60.hasData()");
    TEST_REQUIRE(!sample.seconds10.has_speed_data(),"IPicoStill.h",0x1e9,"!s.Sensors10.hasSpeedData()");TEST_REQUIRE(!sample.seconds60.has_speed_data(),"IPicoStill.h",0x1ea,"!s.Sensors60.hasSpeedData()");
    sample.current.timestamp=120001;sample.current.temperature[0]=10;sample.update_statistics(120001);
    TEST_REQUIRE(sample.seconds10.has_speed_data(),"IPicoStill.h",0x1f0,"s.Sensors10.hasSpeedData()");TEST_REQUIRE(sample.seconds10.speed[0]==5,"IPicoStill.h",0x1f1,"5.0 == s.Sensors10.T1Speed");TEST_REQUIRE(sample.seconds60.has_speed_data(),"IPicoStill.h",0x1f2,"s.Sensors60.hasSpeedData()");TEST_REQUIRE(sample.seconds60.speed[0]==5,"IPicoStill.h",0x1f3,"5.0 == s.Sensors60.T1Speed");return scope.end();
}
bool still_parser_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    PicoStillSamples sample;bool accepted=false;
    {Scope scope(hooks,"BSTIL","PicoStill.h",0x1f8,"Parser::update() empty");TEST_TRY(scope.begin());TEST_TRY(parse_sample(sample,"",services,accepted));TEST_REQUIRE(!accepted,"PicoStill.h",0x1f9,"!p.update(now, s, buf)");TEST_TRY(scope.end());}
    {Scope scope(hooks,"BSTIL","PicoStill.h",0x1fd,"Parser::update() normal case");TEST_TRY(scope.begin());TEST_TRY(parse_sample(sample,"#1.11,2.22,3.33,4.44,5.55,1,1,1,None#",services,accepted));TEST_REQUIRE(accepted,"PicoStill.h",0x1ff,"p.update(now, s, buf)");
        const double temperatures[]={1.11,2.22,3.33,4.44};const char* expressions[]={"s.T1 == TEMP_F_TO_C((double)1.11)","s.T2 == TEMP_F_TO_C((double)2.22)","s.T3 == TEMP_F_TO_C((double)3.33)","s.T4 == TEMP_F_TO_C((double)4.44)"};
        for(unsigned i=0;i<4;++i)TEST_REQUIRE(sample.current.temperature[i]==(temperatures[i]-32)/1.8,"PicoStill.h",0x200+i,expressions[i]);
        TEST_REQUIRE(sample.current.pressure==5.55,"PicoStill.h",0x204,"s.Psi == 5.55");TEST_REQUIRE(sample.current.pump_running,"PicoStill.h",0x205,"s.IsPumpRunning");TEST_REQUIRE(sample.current.fan_running,"PicoStill.h",0x206,"s.IsFanRunning");TEST_REQUIRE(sample.current.program_running,"PicoStill.h",0x207,"s.IsProgramRunning");TEST_REQUIRE(!sample.current.error[0],"PicoStill.h",0x208,"!s.hasError()");TEST_TRY(scope.end());}
    const char* names[]={"Parser::update() test integer","Parser::update() missing field","Parser::update() Some Error"};const char* texts[]={"#1,2,3,4,5,1,1,0,None#","#1,2,3,4,5,0,0,0,#","#1,2,3,4,5,0,0,0,Some Error#"};const std::uint32_t lines[]={0x20c,0x213,0x21a};
    for(unsigned i=0;i<3;++i){Scope scope(hooks,"BSTIL","PicoStill.h",lines[i],names[i]);TEST_TRY(scope.begin());TEST_TRY(parse_sample(sample,texts[i],services,accepted));TEST_REQUIRE(accepted,"PicoStill.h",lines[i]+2,"p.update(now, s, buf)");
        TEST_REQUIRE((sample.current.error[0]!=0)==(i==2),"PicoStill.h",lines[i]+3,i==2?"s.hasError()":"!s.hasError()");if(i==2)TEST_REQUIRE(std::strcmp(sample.current.error.data(),"Some Error")==0,"PicoStill.h",0x21e,"CString::areEquals(\"Some Error\", s.Error.c_ptr())");TEST_TRY(scope.end());}
    return true;
}
bool still_backend_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    HttpClient client;StillAuthorization backend(client);backend.services.diagnostics=services;
    {Scope scope(hooks,"BSTIL","PicoStill.h",0xd0,"BackendClient_T::generateJson()");TEST_TRY(scope.begin());TEST_TRY(backend.prepare_body("ZseriesUid","stillUid",false)==StillAuthorizationResult::Ok);
        TEST_REQUIRE(backend.length>0,"PicoStill.h",0xd4,"buf.length() > 0");TEST_REQUIRE(std::strcmp(backend.buffer.data(),"{\"MachineUID\":\"ZseriesUid\",\"MachineType\":2,\"PicoStillUID\":\"stillUid\",\"HasCleanedAck\":false}")==0,"PicoStill.h",0xdc,"CString::areEquals(expected, buf.c_ptr())");TEST_TRY(scope.end());}
    bool accepted=false;
    {Scope scope(hooks,"BSTIL","PicoStill.h",0xe0,"BackendClient_T::parseJson() Test1");TEST_TRY(scope.begin());TEST_TRY(parse_authorization(backend,"{}",services,accepted));TEST_REQUIRE(accepted,"PicoStill.h",0xe4,"parseJson(json, state)");TEST_TRY(authorization_out(services,backend.record));
        TEST_REQUIRE(!backend.record.cleaned(),"PicoStill.h",0xe6,"!state.hasCleanedAck()");TEST_REQUIRE(backend.record.profile_id==0,"PicoStill.h",0xe7,"0 == state.profileId");TEST_REQUIRE(!backend.record.uid[0],"PicoStill.h",0xe8,"CString::isNullOrEmpty(state.uid.c_ptr())");TEST_TRY(scope.end());}
    const char* prefix="{\"MachineUID\": \"240ac4840ee4\",\"MachineType\": 2,\"PicoStillUID\": \"30aea47a9240\",\"HasCleanedAck\": true,\"PicoStill\": {\"ID\": 5,\"UID\": \"30aea47a9240\",\"ProfileID\": 27081,\"CreationDate\": \"2018-02-23T22:00:38.827\",\"CurrentFirmware\": \"0.0.4\",\"UpdateToFirmware\": \"0.0.4\",\"LastCommunication\": \"2018-12-19T15:24:40.62\",\"SerialNumber\": null,\"CleanedAckDate\": ";
    for(unsigned i=0;i<2;++i){Scope scope(hooks,"BSTIL","PicoStill.h",i?0x108:0xec,i?"BackendClient_T::parseJson() Test3":"BackendClient_T::parseJson() Test2");TEST_TRY(scope.begin());
        char json[513];::snprintf(json,sizeof(json),"%s%s}}",prefix,i?"\"2018-12-26T16:27:11.7185802Z\"":"null");TEST_TRY(parse_authorization(backend,json,services,accepted));TEST_REQUIRE(accepted,"PicoStill.h",i?0x11d:0x100,"parseJson(json, state)");TEST_TRY(authorization_out(services,backend.record));
        TEST_REQUIRE(backend.record.profile_id==27081,"PicoStill.h",i?0x11f:0x102,"27081 == state.profileId");
        if(!i)TEST_REQUIRE(std::strcmp(backend.record.uid.data(),"30aea47a9240")==0,"PicoStill.h",0x103,"CString::areEquals(\"30aea47a9240\", state.uid.c_ptr())");
        TEST_REQUIRE(backend.record.cleaned()==(i!=0),"PicoStill.h",i?0x120:0x104,i?"state.hasCleanedAck()":"!state.hasCleanedAck()");TEST_TRY(scope.end());}
    return true;
}
bool still_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks){return still_sensors_console_self_test(services,hooks)&&still_state_console_self_test(services,hooks)&&still_parser_console_self_test(services,hooks)&&still_backend_console_self_test(services,hooks);}
bool software_console_test(std::uint32_t callback,CoreApplication& application,const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    SoftwareConsoleClock clock;clock.context=application.ports.context;clock.millis=application.ports.millis;
    switch(callback){case 0x400e0e70:return runtime_console_self_test(services,hooks,clock);case 0x400eea38:return still_sensors_console_self_test(services,hooks);case 0x400f610c:return still_state_console_self_test(services,hooks);case 0x4010d824:return still_parser_console_self_test(services,hooks);case 0x40119eb8:case 0x4011a1fc:return still_backend_console_self_test(services,hooks);case 0x4011a204:return still_console_self_test(services,hooks);default:return false;}
}
#undef TEST_REQUIRE
#undef TEST_TRY
}} // namespace zseries::oem
