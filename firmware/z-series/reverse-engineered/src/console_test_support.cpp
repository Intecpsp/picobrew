#include "zseries/console_test_support.hpp"
#include <cstdio>
#include <cstring>
namespace zseries { namespace oem {
ConsoleTestMarker::ConsoleTestMarker(const ConsoleServices& services,
        const ApplicationConsoleHooks& hooks,const char* category) noexcept:
    services_(services),hooks_(hooks),category_(category){}
bool ConsoleTestMarker::begin(){
    if(active_||!hooks_.millis||!hooks_.assertion_count||!hooks_.test_message||
       !hooks_.write||!services_.assertion||!services_.log)return false;
    if(!category_||!*category_){
        if(!services_.assertion(services_.context,"Core.h",0x1d0,"!CString::isNullOrEmpty(_category)"))return false;
        // Null is not a readable display string. The named OEM categories
        // are all bounded strings; retain the assertion for a bad caller.
        if(!category_)return false;
    }
    if(std::strlen(category_)>59)return false;
    date_=hooks_.millis(hooks_.context);
    assertions_=hooks_.assertion_count(hooks_.context);
    active_=true;
    return hooks_.test_message(hooks_.context,category_);
}
bool ConsoleTestMarker::end(){
    if(!active_)return false;
    active_=false;
    const bool passed=hooks_.assertion_count(hooks_.context)==assertions_;
    if(!passed&&!services_.assertion(services_.context,"Core.h",0x1d9,"failedCount == 0"))return false;
    char result[65],output[80];
    const int length=::snprintf(result,sizeof result,"%s %s",category_,passed?"OK":"ERR");
    if(length<0||static_cast<std::size_t>(length)>=sizeof result)return false;
    const int bytes=::snprintf(output,sizeof output,passed?"\x1b[32m%s\x1b[0m\r\n":"\x1b[31m%s\x1b[0m\r\n",result);
    if(bytes<0||static_cast<std::size_t>(bytes)>=sizeof output||
       !hooks_.write(hooks_.context,output,static_cast<std::size_t>(bytes))||
       !hooks_.test_message(hooks_.context,result))return false;
    ::snprintf(output,sizeof output,"PicoBrew::pbassertCount = %d",static_cast<std::int32_t>(hooks_.assertion_count(hooks_.context)));
    if(!services_.log(services_.context,ConsoleLevel::Info,"CORE",nullptr,0,output))return false;
    const auto elapsed=hooks_.millis(hooks_.context)-date_;
    const int size=::snprintf(output,sizeof output,"%s ran for %u sec",category_,elapsed/1000);
    return size>=0&&static_cast<std::size_t>(size)<sizeof output&&
        services_.log(services_.context,ConsoleLevel::Warning,"CORE","Core.h",0x1e2,output);
}
ConsoleTestMarker::~ConsoleTestMarker(){if(active_)(void)end();}
}} // namespace zseries::oem
