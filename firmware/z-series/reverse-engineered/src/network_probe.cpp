#include "zseries/network_probe.hpp"
#include <cstdio>
namespace zseries { namespace oem {
namespace {
NetworkResult log(const NetworkProbeServices& io,std::uint32_t line,const char* message){
    const auto& out=io.diagnostics;
    if(!out.log)return NetworkResult::MissingService;
    return out.log(out.context,line==0x42?ConsoleLevel::Warning:ConsoleLevel::Error,"ZCLI","ZClient.h",line,message)
        ?NetworkResult::Ok:NetworkResult::ServiceFailure;
}
}
NetworkResult check_network_connectivity(std::uint32_t type,const NetworkProbeServices& io,bool& connected){
    connected=false;
    if(type==2){
        if(!io.wifi_status)return NetworkResult::MissingService;
        std::int32_t status=0;if(!io.wifi_status(io.context,status))return NetworkResult::ServiceFailure;
        connected=status!=1&&status!=4&&status!=5&&status!=6&&status!=255;return NetworkResult::Ok;
    }
    if(type!=1)return log(io,0xbb,"Unexpected NetworkType");
    if(!io.ethernet_acquire||!io.ethernet_connect||!io.ethernet_stop)return NetworkResult::MissingService;
    bool available=false;if(!io.ethernet_acquire(io.context,available))return NetworkResult::ServiceFailure;
    if(!available)return log(io,0x3c,"Cannot getClient() for testing");
    std::int32_t result=0;
    const bool completed=io.ethernet_connect(io.context,"www.picobrewtest.com",80,result);
    const bool stopped=io.ethernet_stop(io.context);
    if(!completed||!stopped)return NetworkResult::ServiceFailure;
    connected=result==1;
    if(!connected){char text[80];::snprintf(text,sizeof text,"ERROR EthernetClient::connect() returned %ld",static_cast<long>(result));return log(io,0x42,text);}
    return NetworkResult::Ok;
}
}} // namespace zseries::oem
