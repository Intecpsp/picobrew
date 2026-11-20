#include "http_client_platform.hpp"
#include <Arduino.h>
#include <IPAddress.h>
#include <cerrno>
namespace zseries { namespace oem { namespace esp32 {
namespace {
std::uint32_t clock(void*){return ::millis();}
bool perform(void* context,const HttpAction& action,std::uint32_t& result){
    auto& binding=*static_cast<HttpBindings*>(context);result=0;
    if(action.effect==HttpEffect::AcquireClient){
        if(!binding.acquire||!action.acquired_class)return false;
        result=binding.acquire(binding.context,action.argument!=0,binding.client,*action.acquired_class)&&binding.client;return true;
    }
    if(action.effect==HttpEffect::FreeHeap){result=xPortGetFreeHeapSize();return true;}
    if(action.effect==HttpEffect::SocketError){result=static_cast<std::uint32_t>(errno);return true;}
    if(!binding.client)return false;
    switch(action.effect){
    case HttpEffect::PrepareEthernet:
        if(!binding.prepare_ethernet)return false;
        errno=0;return binding.prepare_ethernet(binding.context,action.argument!=0);
    case HttpEffect::ConnectHost:result=static_cast<std::uint32_t>(binding.client->connect(action.text,static_cast<std::uint16_t>(action.argument)));break;
    case HttpEffect::ConnectAddress:result=static_cast<std::uint32_t>(binding.client->connect(IPAddress(action.second),static_cast<std::uint16_t>(action.argument)));break;
    case HttpEffect::Write:result=static_cast<std::uint32_t>(binding.client->write(action.data,action.size));break;
    case HttpEffect::Flush:binding.client->flush();break;
    case HttpEffect::Available:result=static_cast<std::uint32_t>(binding.client->available());break;
    case HttpEffect::ReadByte:result=static_cast<std::uint32_t>(binding.client->read());break;
    case HttpEffect::ReadBytes:result=static_cast<std::uint32_t>(binding.client->readBytes(action.destination,action.size));break;
    case HttpEffect::Connected:result=binding.client->connected();break;
    case HttpEffect::Stop:binding.client->stop();break;
    default:return false;
    }return true;
}
}
HttpServices http_services(HttpBindings& binding) noexcept {HttpServices services;services.context=&binding;services.millis=clock;services.perform=perform;return services;}
}}} // namespace zseries::oem::esp32
