#include "azure_mqtt_platform.hpp"
#include "azure_crypto.hpp"
#include "zseries/azure_console_tests.hpp"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace zseries { namespace oem { namespace esp32 {
namespace {
std::uint32_t now(void*){return ::millis();}
Client* client(void* p){return static_cast<AzureMqttBinding*>(p)->network.http.client;}
bool connected(void* p,bool& value){if(!client(p))return false;value=client(p)->connected()!=0;return static_cast<AzureMqttBinding*>(p)->transport_ok();}
bool connect_host(void* p,const char* host,std::uint16_t port,int& value){if(!client(p))return false;value=client(p)->connect(host,port);return static_cast<AzureMqttBinding*>(p)->transport_ok();}
bool connect_ip(void* p,std::uint32_t address,std::uint16_t port,int& value){if(!client(p))return false;value=client(p)->connect(IPAddress(address),port);return static_cast<AzureMqttBinding*>(p)->transport_ok();}
bool write(void* p,const std::uint8_t* bytes,std::size_t size,std::size_t& value){if(!client(p))return false;value=client(p)->write(bytes,size);return static_cast<AzureMqttBinding*>(p)->transport_ok();}
bool available(void* p,int& value){if(!client(p))return false;value=client(p)->available();return static_cast<AzureMqttBinding*>(p)->transport_ok();}
bool read(void* p,int& value){if(!client(p))return false;value=client(p)->read();return static_cast<AzureMqttBinding*>(p)->transport_ok();}
bool flush(void* p){if(!client(p))return false;client(p)->flush();return static_cast<AzureMqttBinding*>(p)->transport_ok();}
bool stop(void* p){if(!client(p))return false;client(p)->stop();return static_cast<AzureMqttBinding*>(p)->transport_ok();}
bool perform(void* p,const AzureAction& a,std::uint32_t& value){return static_cast<AzureMqttBinding*>(p)->perform(a,value);}
bool assertion(void* p,std::uint32_t line){return static_cast<AzureMqttBinding*>(p)->assertion(line);}
int hmac(void* p,const std::uint8_t* key,std::size_t key_size,const std::uint8_t* bytes,std::size_t size,std::uint8_t out[32]){
    auto& self=*static_cast<AzureMqttBinding*>(p);self.hmac_error=azure_hmac_sha256(nullptr,key,key_size,bytes,size,out);return self.hmac_error;
}
bool info(void* p){return static_cast<AzureMqttBinding*>(p)->information();}
bool tests(void* p){return static_cast<AzureMqttBinding*>(p)->developer_tests();}
}
AzureMqttBinding::AzureMqttBinding(std::uint32_t& mode,const ConsoleServices& log,Print* serial):network(mode,log,serial),diagnostics(log){}
void AzureMqttBinding::attach(AzureService& service){
    azure=&service;AzureServices s;s.context=this;s.millis=now;s.perform=esp32::perform;s.assertion=esp32::assertion;s.hmac=hmac;s.diagnostics=diagnostics;service.services=s;
}
bool AzureMqttBinding::transport_ok() const noexcept {
    return network.factory.kind!=HttpClientClass::Ethernet||!network.http.client||static_cast<EthernetTransport*>(network.http.client)->state.callback_result==NetworkResult::Ok;
}
bool AzureMqttBinding::perform(const AzureAction& action,std::uint32_t& value){
    value=0;bool accepted=false;protocol_result=MqttResult::Ok;
    switch(action.effect){
    case AzureEffect::Connected:protocol_result=mqtt.connected(accepted);value=accepted?1:0;break;
    case AzureEffect::Disconnect:protocol_result=mqtt.disconnect();break;
    case AzureEffect::AcquireClient:{std::uintptr_t handle=0;HttpClientClass kind=HttpClientClass::None;
        factory_result=network.factory.acquire(network.network,action.argument!=0,handle,kind,accepted);
        network.http.client=reinterpret_cast<Client*>(handle);value=accepted&&handle?1:0;return factory_result==NetworkResult::Ok;}
    case AzureEffect::BindClient:{if(!network.http.client)return false;MqttTransport io;io.context=this;io.millis=now;io.connected=connected;io.connect_host=connect_host;io.connect_ip=connect_ip;io.write=write;io.available=available;io.read=read;io.flush=flush;io.stop=stop;mqtt.bind(io);break;}
    case AzureEffect::SetServer:if(!action.text)return false;mqtt.server(action.text,static_cast<std::uint16_t>(action.argument));break;
    case AzureEffect::SetCallback:if(!action.message)return false;mqtt.message_context=action.callback_context;mqtt.message=action.message;break;
    case AzureEffect::Connect:{MqttConnect options;options.client_id=action.text;options.username=action.second;options.password=action.third;protocol_result=mqtt.connect(options,accepted);value=accepted?1:0;break;}
    case AzureEffect::Subscribe:protocol_result=mqtt.subscribe(action.text,0,accepted);value=accepted?1:0;break;
    case AzureEffect::Loop:protocol_result=mqtt.loop(accepted);value=accepted?1:0;break;
    case AzureEffect::Publish:if(!action.second)return false;protocol_result=mqtt.publish(action.text,action.second,std::strlen(action.second),action.argument!=0,accepted);value=accepted?1:0;break;
    case AzureEffect::Diagnostic:return diagnostic(action.argument);
    default:return false;
    }
    return protocol_result==MqttResult::Ok;
}
bool AzureMqttBinding::assertion(std::uint32_t line){
    if(!diagnostics.assertion)return false;
    const char* file="Azure.h";const char* expression=nullptr;
    switch(line){
    case 0xf:file="Json.h";expression="isEnumValid<EClass>(e)";break;
    case 0x107:expression="isEnumValid<AzureIoTCommand>(static_cast<AzureIoTCommand>(iotCommand))";break;
    case 0x124:expression="iotCommand != AzureIoTCommand::Unknown";break;
    case 0x12a:expression="isEnumValid<RecipeKind>(static_cast<RecipeKind>(kind))";break;
    case 0x15c:expression="_connectCalled";break;
    case 0x171:expression="!CString::isNullOrEmpty(deviceId)";break;
    case 0x172:expression="!CString::isNullOrEmpty(ttl)";break;
    case 0x17a:expression="!uri.isTruncated()";break;
    case 0x237:expression="_sas_key_len > 0";break;
    default:return false;
    }
    return diagnostics.assertion(diagnostics.context,file,line,expression);
}
bool AzureMqttBinding::diagnostic(std::uint32_t line){
    if(!diagnostics.log)return false;
    const char* tag="AZ";const char* text=nullptr;ConsoleLevel level=ConsoleLevel::Info;char formatted[96];
    switch(line){
    case 0x32:level=ConsoleLevel::Warning;text="SessionType is invalid";break;
    case 0x37:level=ConsoleLevel::Warning;text="ZProgramId is invalid";break;
    case 0x3c:level=ConsoleLevel::Warning;text="ZProgramId is ZProgramId::None";break;
    case 0x61:level=ConsoleLevel::Warning;text="!params.containsKey(\"ProgramParams\")";break;
    case 0xdc:text="ATTEMPTING IOT CONNECTION";break;
    case 0xdf:text="IOT CONNECTION MADE";break;
    case 0xe2:level=ConsoleLevel::Error;text="IOT CONNECTION FAILED";break;
    case 0xeb:text="Group Brew Disabled";break;
    case 0xf6:level=ConsoleLevel::Warning;text="Ignoring message (wrong state)";break;
    case 0xfe:level=ConsoleLevel::Warning;text="Ignoring message (json error)";break;
    case 0x110:level=ConsoleLevel::Error;text="Firmware Versions different";break;
    case 0x143:level=ConsoleLevel::Error;text="Unknown RecipeKind";break;
    case 0x14a:level=ConsoleLevel::Error;text="Unknown AzureioTCommand";break;
    case 0x161:level=ConsoleLevel::Error;text="SYS CLIENT UNAVAILABLE";break;
    case 0x165:text="SYS CLIENT AVAILABLE";break;
    case 0x191:case 0x1b2:level=ConsoleLevel::Error;tag="SHA";::snprintf(formatted,sizeof(formatted),line==0x191?"mbedtls_md_hmac() error %d":"_generateHmac() error %d",hmac_error);text=formatted;break;
    case 0x209:text="Bad AzureState";break;
    case 0x289:text="Session loaded window timed out";break;
    default:return false;
    }
    return diagnostics.log(diagnostics.context,level,tag,"Azure.h",line,text);
}
AzureConsoleCallbacks AzureMqttBinding::console_callbacks() noexcept {AzureConsoleCallbacks callbacks;callbacks.azure=azure;callbacks.context=this;callbacks.information=info;callbacks.tests=tests;return callbacks;}
bool AzureMqttBinding::information(){
    if(!diagnostics.log||!network.serial)return false;
    if(!diagnostics.log(diagnostics.context,ConsoleLevel::Info,"AZ","Azure.h",0x290,"NOT REAL TEXT"))return false;
    const double timestamp=static_cast<double>(static_cast<float>(::millis())/1000.0f);
    bool active=false;protocol_result=mqtt.connected(active);if(protocol_result!=MqttResult::Ok)return false;
    char row[100];::snprintf(row,sizeof(row),"[IV][%8.2f][AZ] _client.connected() = %s\r\n",timestamp,active?"True":"False");return network.serial->print(row)==std::strlen(row);
}
bool AzureMqttBinding::developer_tests(){
    return azure&&azure_password_tests(*azure,diagnostics)==ConsoleResult::Ok;
}
}}} // namespace zseries::oem::esp32
