#include "zseries/pinger.hpp"
#include <cstdio>
#include <cstring>
namespace zseries { namespace oem {
PingerResult Pinger::assertion(std::uint32_t line,const char* expression){
    const auto& s=services.diagnostics;
    if(!s.assertion)return PingerResult::MissingService;
    return s.assertion(s.context,"Pinger.h",line,expression)?PingerResult::Ok:PingerResult::ServiceFailure;
}
PingerResult Pinger::diagnostic(ConsoleLevel level,std::uint32_t line,const char* message){
    const auto& s=services.diagnostics;
    if(!s.log)return PingerResult::MissingService;
    return s.log(s.context,level,"PING","Pinger.h",line,message)?PingerResult::Ok:PingerResult::ServiceFailure;
}
PingerResult Pinger::setup(){
    if(!services.set_option||!services.register_commands)return PingerResult::MissingService;
    if(!services.set_option(services.context,51,10)||!services.set_option(services.context,52,1000)||
       !services.set_option(services.context,53,500)||!services.register_commands(services.context))return PingerResult::ServiceFailure;
    return PingerResult::Ok;
}
PingerResult Pinger::ping(std::uint32_t address,bool& started){
    started=false;
    if(address==0xffffffffU){const auto r=assertion(0x28,"target != INADDR_NONE");if(r!=PingerResult::Ok)return r;}
    if(remaining>0){const auto r=assertion(0x29,"!isBusy()");if(r!=PingerResult::Ok)return r;}
    if(!services.wifi_status)return PingerResult::MissingService;
    std::int32_t status=0;if(!services.wifi_status(services.context,status))return PingerResult::ServiceFailure;
    if(address==0xffffffffU)return diagnostic(ConsoleLevel::Error,0x2d,"Invalid target argument");
    if(status!=3)return diagnostic(ConsoleLevel::Error,0x2f,"WiFi not connected");
    if(remaining>0)return diagnostic(ConsoleLevel::Error,0x31,"Already busy");
    if(!services.set_option||!services.start)return PingerResult::MissingService;
    target=address;remaining=11;
    std::int32_t error=0;
    if(!services.set_option(services.context,50,address)||!services.start(services.context,error))return PingerResult::ServiceFailure;
    started=error==0;return PingerResult::Ok;
}
PingerResult Pinger::hostname(const char* name,bool& started){
    started=false;
    if(!name||!*name){
        const auto r=assertion(0x43,"!CString::isNullOrEmpty(hostname)");if(r!=PingerResult::Ok)return r;
        return diagnostic(ConsoleLevel::Error,0x47,"Invalid hostname argument (null or empty)");
    }
    if(!services.resolve)return PingerResult::MissingService;
    // The original accepts a borrowed C string. The owned diagnostic buffer
    // limits formatting only; DNS receives the complete original argument.
    char message[320];::snprintf(message,sizeof message,"Resolve hostname \"%s\"",name);
    auto r=diagnostic(ConsoleLevel::Info,0x49,message);if(r!=PingerResult::Ok)return r;
    std::uint32_t resolved=0;std::int32_t found=0;
    if(!services.resolve(services.context,name,resolved,found))return PingerResult::ServiceFailure;
    if(found!=1){::snprintf(message,sizeof message,"Cannot resolve IP of hostname \"%s\"",name);return diagnostic(ConsoleLevel::Error,0x4d,message);}
    ::snprintf(message,sizeof message,"hostname = \"%s\", ip = %d.%d.%d.%d",name,
        static_cast<int>(resolved&255),static_cast<int>((resolved>>8)&255),static_cast<int>((resolved>>16)&255),static_cast<int>(resolved>>24));
    r=diagnostic(ConsoleLevel::Info,0x4f,message);if(r!=PingerResult::Ok)return r;
    return ping(resolved,started);
}
PingerResult Pinger::address(const char* text,bool& started){
    started=false;
    if(!text||!*text){
        const auto r=assertion(0x59,"!CString::isNullOrEmpty(ip)");if(r!=PingerResult::Ok)return r;
        return diagnostic(ConsoleLevel::Error,0x5c,"Invalid ip argument (null or empty)");
    }
    if(!services.parse)return PingerResult::MissingService;
    std::uint32_t parsed=0;if(!services.parse(services.context,text,parsed))return PingerResult::ServiceFailure;
    return ping(parsed,started);
}
PingerResult Pinger::receive(const PingStatistics& data){
    if(remaining<1){const auto r=assertion(0xa1,"_left > 0");if(r!=PingerResult::Ok)return r;}
    // Defined two's-complement decrement, including the original underflow.
    const std::uint32_t bits=static_cast<std::uint32_t>(remaining)-1U;
    std::int32_t value;std::memcpy(&value,&bits,sizeof value);remaining=value;
    const double average=data.received?static_cast<double>(static_cast<float>(data.total_ms)/static_cast<float>(data.received)):0.0;
    char message[320];::snprintf(message,sizeof message,
        "STATS Sent:%d Recv:%d Err:%d Timeout:%d resp:%dms total:%dms avg:%.1fms min:%dms max:%dms",
        static_cast<int>(data.sent),static_cast<int>(data.received),static_cast<int>(data.errors),static_cast<int>(data.timeouts),
        static_cast<int>(data.response_ms),static_cast<int>(data.total_ms),average,static_cast<int>(data.minimum_ms),static_cast<int>(data.maximum_ms));
    return diagnostic(ConsoleLevel::Info,0xad,message);
}
}} // namespace zseries::oem
