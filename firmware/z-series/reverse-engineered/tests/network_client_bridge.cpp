#include "zseries/network_client.hpp"
#include "zseries/diagnostics.hpp"
#include <cstring>
#include <string>
#include <vector>
using namespace zseries::oem;
namespace {
std::vector<std::string> events;
struct Context { const std::uint32_t* v; std::uint32_t now; DiagnosticState diagnostics; unsigned reads=0,writes=0; bool phase=false; std::uint32_t timeout=444,secure=1,ca=0x1234; };
std::uint32_t clock(void* opaque){return static_cast<Context*>(opaque)->now;}
void delay_ms(void* opaque,std::uint32_t duration){auto& c=*static_cast<Context*>(opaque);events.push_back("D|"+std::to_string(duration)+"|"+std::to_string(c.diagnostics.assertion_count));c.now+=duration;}
void assertion_log(void* opaque,double seconds,const char* file,std::uint32_t line,const char* expression){auto& c=*static_cast<Context*>(opaque);std::uint64_t bits;std::memcpy(&bits,&seconds,8);events.push_back(std::string("A|")+file+"|"+std::to_string(line)+"|"+expression+"|"+std::to_string(static_cast<std::uint32_t>(bits))+"|"+std::to_string(static_cast<std::uint32_t>(bits>>32))+"|"+std::to_string(c.diagnostics.assertion_count));}
bool assertion(void* opaque,const char* file,std::uint32_t line,const char* expression){DiagnosticServices s;s.context=opaque;s.millis=clock;s.delay_ms=delay_ms;s.assertion_log=assertion_log;return record_assertion(static_cast<Context*>(opaque)->diagnostics,s,file,line,expression)==DiagnosticResult::Ok;}
std::uint32_t hash(const std::uint8_t* data){std::uint32_t value=2166136261u;for(unsigned i=0;i<64;++i)value=(value^data[i])*16777619u;return value;}
bool factory(void* opaque,const ClientAction& action,std::uintptr_t& result){auto& c=*static_cast<Context*>(opaque);unsigned kind=static_cast<unsigned>(action.kind);result=0;
    switch(action.operation){
    case ClientOperation::Create:events.push_back("C|"+std::to_string(kind));result=0x3ff60000u+(kind-1)*0x1000;if(kind==1){c.timeout=120000;c.secure=0;}break;
    case ClientOperation::Stop:events.push_back("S|"+std::to_string(kind));break;
    case ClientOperation::Destroy:events.push_back("X|"+std::to_string(kind));break;
    case ClientOperation::Connected:events.push_back("N|"+std::to_string(kind));result=(c.v[5]>>(kind-1))&1;break;
    case ClientOperation::Timeout:if(kind==1)c.timeout=action.argument;else events.push_back("T|"+std::to_string(kind)+"|"+std::to_string(action.argument));break;
    case ClientOperation::PrepareEthernet:c.secure=action.argument;c.ca=0;break;
    }return true;
}
bool operation(void* opaque,const SocketAction& action,std::int32_t& result){auto& c=*static_cast<Context*>(opaque);const auto* v=c.v;result=0;
    events.push_back("P|"+std::to_string(static_cast<unsigned>(action.operation))+"|"+std::to_string(action.socket)+"|"+std::to_string(action.argument)+"|"+std::to_string(action.second)+"|"+std::to_string(static_cast<std::uint32_t>(action.size)));
    switch(action.operation){
    case SocketOperation::Status:
        if(v[0]==3){if(!c.phase)result=action.socket==v[4]?v[5]:0x17;else{unsigned n=c.reads++;result=v[10]==0?0x17:v[10]==1?(n==0?0x15:0x17):v[10]==2?0:(n==0?0x15:0);}}
        else if(v[0]==9)result=c.reads++>=v[10]?0:0x17;else result=v[10];
        break;
    case SocketOperation::Open:result=1;break;
    case SocketOperation::Connect:c.phase=true;result=v[6];break;
    case SocketOperation::Send:result=v[11];break;
    case SocketOperation::Receive:case SocketOperation::TlsRead:{
        result=static_cast<std::int32_t>(action.operation==SocketOperation::Receive?v[12]:v[13]);
        if(action.destination&&static_cast<std::int32_t>(action.size)>0)for(unsigned i=0;i<64&&i<action.size&&i<static_cast<unsigned>(result>0?result:0);++i)action.destination[i]=0x40+i%29;
        break;}
    case SocketOperation::Available:case SocketOperation::TlsAvailable:result=v[16];break;
    case SocketOperation::Peek:result=v[17];break;
    case SocketOperation::TlsStart:result=v[15];break;
    case SocketOperation::TlsWrite:result=c.writes++<v[14]?(c.writes%2?-0x6900:-0x6880):static_cast<std::int32_t>(v[11]);break;
    case SocketOperation::TlsError:result=static_cast<std::int32_t>(action.argument);break;
    default:break;
    }return true;
}
}
extern "C" void network_client_case(const std::uint32_t* v,std::uint32_t* output){
    events.clear();Context c;c.v=v;c.now=v[7];c.diagnostics.assertion_count=v[8];
    ClientFactory f;f.kind=static_cast<HttpClientClass>(v[3]);f.services.context=&c;f.services.perform=factory;f.services.diagnostics.context=&c;f.services.diagnostics.assertion=assertion;
    for(unsigned i=0;i<3;++i)if(v[4]&(1u<<i))f.handles[i]=0x3ff60000u+i*0x1000;
    EthernetSocket s;s.socket=v[1];s.secure=v[2];s.tls_active=v[3];s.write_error=v[19];s.services.context=&c;s.services.millis=clock;s.services.delay_ms=delay_ms;s.services.perform=operation;
    std::uint8_t buffer[64];std::memset(buffer,0xa5,sizeof(buffer));std::uint16_t port=v[9];
    std::uintptr_t handle=0;HttpClientClass kind=HttpClientClass::None;bool accepted=false;std::uint32_t returned=0;
    switch(v[0]){
    case 0:returned=static_cast<unsigned>(f.acquire(v[1],v[2],handle,kind,accepted));break;
    case 1:returned=static_cast<unsigned>(f.clear());break;
    case 2:returned=static_cast<unsigned>(f.acquire(1,true,handle,kind,accepted));if(!returned)returned=static_cast<unsigned>(f.acquire(2,false,handle,kind,accepted));if(!returned)returned=static_cast<unsigned>(f.acquire(2,true,handle,kind,accepted));break;
    case 3:returned=s.connect(0x04030201,443,port);break;
    case 4:returned=s.connected();break;
    case 5:returned=static_cast<std::uint32_t>(s.write(buffer,v[18]));break;
    case 6:returned=s.read(buffer,v[18]);break;
    case 7:returned=s.available();break;
    case 8:returned=s.peek();break;
    case 9:s.stop();break;
    case 10:returned=s.read();break;
    case 11:s.flush();break;
    }
    unsigned mask=0;for(unsigned i=0;i<3;++i)if(f.handles[i])mask|=1u<<i;
    const bool fac=v[0]<3;
    output[0]=returned;output[1]=fac?0:s.socket;output[2]=fac?0:s.tls_active;output[3]=fac?0:s.write_error;output[4]=fac?0:port;
    output[5]=fac?static_cast<unsigned>(f.kind):0;output[6]=fac?mask:0;output[7]=handle?static_cast<unsigned>((handle-0x3ff60000u)/0x1000+1):0;
    output[8]=static_cast<unsigned>(kind);output[9]=accepted;output[10]=c.now;output[11]=c.diagnostics.assertion_count;
    output[12]=hash(buffer);output[13]=static_cast<unsigned>(s.callback_result);
    output[14]=fac&&f.handles[0]?c.timeout:0;output[15]=fac&&f.handles[0]?c.secure:0;output[16]=fac&&f.handles[0]?c.ca:0;
}
extern "C" unsigned network_client_event_count(){return events.size();}
extern "C" const char* network_client_event(unsigned index){return events.at(index).c_str();}
extern "C" unsigned network_client_guard(unsigned mode){
    ClientFactory f; std::uintptr_t handle=9; HttpClientClass kind=HttpClientClass::Ethernet; bool accepted=true;
    if(mode==0)return static_cast<unsigned>(f.acquire(1,false,handle,kind,accepted));
    if(mode==1){f.handles[0]=23;f.services.perform=[](void*,const ClientAction&,std::uintptr_t&){return false;};return static_cast<unsigned>(f.clear())+(f.handles[0]==23?256:0);}
    if(mode==2){f.services.perform=[](void*,const ClientAction&,std::uintptr_t& value){value=0;return true;};return static_cast<unsigned>(f.acquire(2,false,handle,kind,accepted));}
    EthernetSocket s;
    if(mode==3){std::uint16_t port=1024;s.connect(1,80,port);return static_cast<unsigned>(s.callback_result);}
    if(mode==4){s.write(nullptr,1);return static_cast<unsigned>(s.callback_result);}
    std::uint32_t input[20]={};Context c;c.v=input;c.now=0;
    s.socket=0;s.tls_active=true;s.services.context=&c;s.services.millis=clock;s.services.delay_ms=delay_ms;
    s.services.perform=[](void*,const SocketAction&,std::int32_t&){return false;};std::uint8_t byte=1;
    s.write(&byte,1);return static_cast<unsigned>(s.callback_result);
}
