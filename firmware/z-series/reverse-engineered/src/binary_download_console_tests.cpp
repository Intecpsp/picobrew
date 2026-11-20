#include "zseries/network_console_tests.hpp"
#include <array>
#include <cstring>

namespace zseries { namespace oem {
namespace {
// The OEM diagnostic instantiates DownloadBinary<16>; production downloads
// instantiate the separately reconstructed 2048-byte specialization.
struct BinaryTest {
    const ConsoleServices& services;
    std::array<std::uint8_t,16> data{};
    std::uint32_t received=0,expected=0;
    bool completed=false,error=false;
    explicit BinaryTest(const ConsoleServices& s):services(s){}
    bool require(bool condition,std::uint32_t line,const char* text){return condition||services.assertion(services.context,"DownloadBinary.h",line,text);}
    bool log(std::uint32_t line,const char* text){return services.log(services.context,ConsoleLevel::Error,"DBIN","DownloadBinary.h",line,text);}
    bool begin(const HttpResponse& response,bool& accepted){
        received=0;expected=static_cast<std::uint32_t>(response.content_length);accepted=false;
        if(std::strcmp(response.content_type.data(),"application/octet-stream")){
            if(!log(0x43,"Unexpected content type"))return false;
            error=true;
        }else if(response.content_length>16){if(!log(0x46,"Not enough space to begin OTA"))return false;error=true;}
        accepted=!error;return true;
    }
    bool body(const std::uint8_t* bytes,std::size_t count){
        if(!require(received<data.size(),0x54,"_ibuf < sizeof(_buf)"))return false;
        if(received>data.size()||(!bytes&&count))return false;
        if(count>data.size()-received){if(!log(0x57,"Internal buffer too short"))return false;error=true;return true;}
        if(count)std::memcpy(data.data()+received,bytes,count);
        received+=static_cast<std::uint32_t>(count);return true;
    }
    bool complete(){if(!require(received==expected,0x4e,"_ibuf == _contentLength"))return false;completed=true;return true;}
};
struct Scope {
    const ApplicationConsoleHooks& hooks;bool active=false;
    explicit Scope(const ApplicationConsoleHooks& value):hooks(value){}
    bool begin(){active=hooks.scope&&hooks.scope(hooks.context,true,"DBIN","DownloadBinary.h",0x76,"downloadBinary_Test1");return active;}
    bool end(){if(!active)return false;active=false;return hooks.scope(hooks.context,false,"DBIN","DownloadBinary.h",0x76,"downloadBinary_Test1");}
    ~Scope(){if(active)(void)end();}
};
}
bool binary_download_console_test(HttpServices transport,const ConsoleServices& services,
        const ApplicationConsoleHooks& hooks){
    if(!transport.perform||!services.assertion||!services.log)return false;
    HttpAction action(HttpEffect::AcquireClient);HttpClientClass kind=HttpClientClass::None;action.acquired_class=&kind;
    std::uint32_t available=0;if(!transport.perform(transport.context,action,available))return false;
    if(!available)return services.log(services.context,ConsoleLevel::Error,"DBIN","DownloadBinary.h",0x67,"Cannot run test without network");
    BinaryTest receiver(services);HttpResponse response;std::strcpy(response.content_type.data(),"application/octet-stream");
    bool accepted=false;response.content_length=8;
    if(!receiver.begin(response,accepted)||!receiver.require(accepted,0x70,"testDbin.begin(response)"))return false;
    response.content_length=16;
    if(!receiver.begin(response,accepted)||!receiver.require(accepted,0x74,"testDbin.begin(response)"))return false;
    Scope scope(hooks);if(!scope.begin())return false;
    // The original uses an uninitialized eight-byte scratch span and never
    // examines its contents. Use initialized bytes while preserving the two
    // real writes, exact byte count, completion and assertions.
    const std::array<std::uint8_t,8> bytes{};
    return receiver.begin(response,accepted)&&receiver.body(bytes.data(),bytes.size())&&
        receiver.body(bytes.data(),bytes.size())&&receiver.complete()&&
        receiver.require(receiver.completed,0x7e,"testDbin.isDone()")&&scope.end();
}
}} // namespace zseries::oem
