#include "zseries/serial_logger.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace zseries { namespace oem {
constexpr std::size_t CsvBuffer::Capacity;
namespace {
SerialLogResult assertion(const SerialLogServices& s,const char* file,std::uint32_t line,const char* expression){
    if(!s.assertion)return SerialLogResult::MissingService;
    return s.assertion(s.context,file,line,expression)?SerialLogResult::Ok:SerialLogResult::ServiceFailure;
}
SerialLogResult emit(const SerialLogServices& s,SerialLogMessage kind,const char* text=nullptr,std::uint32_t value=0){
    if(!s.emit)return SerialLogResult::MissingService;
    return s.emit(s.context,kind,text,value)?SerialLogResult::Ok:SerialLogResult::ServiceFailure;
}
std::uint32_t original_atoi(const char* p){
    // ROM atoi uses the 32-bit strtol domain. Keep host long width out of it.
    while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p=='\f'||*p=='\v')++p;
    bool negative=false;if(*p=='-'||*p=='+'){negative=*p=='-';++p;}
    const std::uint32_t limit=negative?0x80000000u:0x7fffffffu;
    std::uint32_t value=0;
    while(*p>='0'&&*p<='9'){
        const auto digit=static_cast<std::uint32_t>(*p++-'0');
        value=value>(limit-digit)/10?limit:value*10+digit;
    }
    return negative?0u-value:value;
}
bool interval_command(void* p,const char* arg){return static_cast<SerialLogger*>(p)->set_interval(arg)==SerialLogResult::Ok;}
bool info_command(void* p,const char*){return static_cast<SerialLogger*>(p)->information()==SerialLogResult::Ok;}
bool start_command(void* p,const char*){return static_cast<SerialLogger*>(p)->enable(true)==SerialLogResult::Ok;}
bool stop_command(void* p,const char*){return static_cast<SerialLogger*>(p)->enable(false)==SerialLogResult::Ok;}
}
void CsvBuffer::clear() noexcept {text.fill(0);length=0;truncated=false;frozen=false;}
SerialLogResult CsvBuffer::append(const char* value,const SerialLogServices& services){
    return value?append(value,std::strlen(value),services):SerialLogResult::Ok;
}
SerialLogResult CsvBuffer::append(const void* value,std::size_t size,const SerialLogServices& services){
    if(frozen){const auto r=assertion(services,"Buffer.h",0x5d,"!_isFrozen");if(r!=SerialLogResult::Ok)return r;}
    if(!value&&size){const auto r=assertion(services,"Buffer.h",0x5e,"data != nullptr || datalen == 0");if(r!=SerialLogResult::Ok)return r;}
    if(frozen||!value||!size)return SerialLogResult::Ok;
    const auto left=Capacity-length;const auto count=std::min(size,left);
    if(size>left){
        if(!truncated){truncated=true;const auto r=emit(services,SerialLogMessage::BufferTruncated,nullptr,0x66);if(r!=SerialLogResult::Ok)return r;}
        else if(left){const auto r=assertion(services,"Buffer.h",0x68,"count == 0");if(r!=SerialLogResult::Ok)return r;}
        const auto r=emit(services,SerialLogMessage::DroppedBytes,nullptr,static_cast<std::uint32_t>(size-left));if(r!=SerialLogResult::Ok)return r;
    }
    std::memcpy(text.data()+length,value,count);length+=count;text[length]=0;return SerialLogResult::Ok;
}
SerialLogResult CsvBuffer::format(const SerialLogServices& services,const char* format,...){
    if(frozen){const auto r=assertion(services,"Buffer.h",0x7b,"!_isFrozen");if(r!=SerialLogResult::Ok)return r;}
    if(!format){const auto r=assertion(services,"Buffer.h",0x7c,"format != nullptr");if(r!=SerialLogResult::Ok)return r;}
    if(frozen||!format)return SerialLogResult::Ok;
    const auto left=Capacity-length;va_list args;va_start(args,format);
    const auto size=static_cast<std::uint32_t>(::vsnprintf(text.data()+length,left+1,format,args));va_end(args);
    // The OEM printf path marks equality as truncated; raw append uses >.
    if(size>=left&&!truncated){truncated=true;const auto r=emit(services,SerialLogMessage::BufferTruncated,nullptr,0x8b);if(r!=SerialLogResult::Ok)return r;}
    length+=std::min<std::size_t>(size,left);text[length]=0;
    if(truncated&&length!=Capacity)return assertion(services,"Buffer.h",0x8f,"!_isTruncated || _left == 0");
    return SerialLogResult::Ok;
}
SerialLogResult SerialLogger::header(){
    if(!source.header)return SerialLogResult::MissingService;
    buffer.clear();auto r=source.header(source.context,buffer,services);if(r!=SerialLogResult::Ok)return r;
    ever_truncated=ever_truncated||buffer.truncated;
    if(buffer.truncated){r=assertion(services,"CoreHelpers.h",0x3a,"!_buf.isTruncated()");if(r!=SerialLogResult::Ok)return r;}
    return emit(services,SerialLogMessage::Header,buffer.text.data());
}
SerialLogResult SerialLogger::regenerate(){
    if(!source.data)return SerialLogResult::MissingService;
    buffer.clear();const auto r=source.data(source.context,buffer,services);if(r!=SerialLogResult::Ok)return r;
    ever_truncated=ever_truncated||buffer.truncated;return SerialLogResult::Ok;
}
SerialLogResult SerialLogger::out(){
    auto r=regenerate();if(r!=SerialLogResult::Ok)return r;
    if(buffer.truncated){r=assertion(services,"CoreHelpers.h",0x41,"!_buf.isTruncated()");if(r!=SerialLogResult::Ok)return r;}
    return emit(services,SerialLogMessage::Status,buffer.text.data());
}
SerialLogResult SerialLogger::enable(bool value){
    if(enabled==value)return SerialLogResult::Ok;
    enabled=value;return header();
}
SerialLogResult SerialLogger::tick(std::uint32_t now){
    if(!enabled||deadline>=now)return SerialLogResult::Ok;
    deadline=interval_ms+now;return out();
}
SerialLogResult SerialLogger::information(){
    auto r=emit(services,SerialLogMessage::Enabled,enabled?"True":"False",enabled?1:0);if(r!=SerialLogResult::Ok)return r;
    r=emit(services,SerialLogMessage::Interval,nullptr,interval_ms);if(r!=SerialLogResult::Ok)return r;
    r=header();return r==SerialLogResult::Ok?out():r;
}
SerialLogResult SerialLogger::set_interval(const char* value){
    if(!value)return SerialLogResult::InvalidInput;
    auto interval=original_atoi(value);
    if(interval<minimum_interval_ms){const auto r=emit(services,SerialLogMessage::IntervalBelowMinimum,nullptr,0x75);if(r!=SerialLogResult::Ok)return r;interval=minimum_interval_ms;}
    interval_ms=interval;return SerialLogResult::Ok;
}
ConsoleResult register_serial_logger_console(ConsoleShell& shell,SerialLogger& logger){
    const char* tokens[]={"setinterval ","i","start","stop","on","off"};
    const char* descriptions[]={"Sets interval between log (in ms)","Display information","Start logging","Stop logging","Start logging","Stop logging"};
    const ConsoleCallback callbacks[]={interval_command,info_command,start_command,stop_command,start_command,stop_command};
    for(unsigned i=0;i<6;++i){const auto result=shell.add_command(i!=0,tokens[i],descriptions[i],&logger,callbacks[i]);if(result!=ConsoleResult::Ok)return result;}
    return ConsoleResult::Ok;
}
}} // namespace zseries::oem
