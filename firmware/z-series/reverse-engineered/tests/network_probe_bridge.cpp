#include "zseries/network_probe.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
struct Context {std::uint32_t* words;std::uint32_t* effects;unsigned count=0;unsigned fail=0;
    bool record(unsigned id,std::uint32_t value){effects[count++]=id;effects[count++]=value;return fail!=id;}};
extern "C" unsigned network_probe_case(std::uint32_t* words,std::uint32_t* effects,unsigned missing,unsigned fail){
    Context c;c.words=words;c.effects=effects;c.fail=fail;NetworkProbeServices io;io.context=&c;io.diagnostics.context=&c;
    io.wifi_status=[](void* p,std::int32_t& value){auto& c=*static_cast<Context*>(p);value=static_cast<std::int32_t>(c.words[1]);return c.record(1,c.words[1]);};
    io.ethernet_acquire=[](void* p,bool& available){auto& c=*static_cast<Context*>(p);available=c.words[2]&&c.words[3];return c.record(2,0);};
    io.ethernet_connect=[](void* p,const char* host,std::uint16_t port,std::int32_t& value){auto& c=*static_cast<Context*>(p);
        assert(std::strcmp(host,"www.picobrewtest.com")==0);value=static_cast<std::int32_t>(c.words[4]);return c.record(3,port);};
    io.ethernet_stop=[](void* p){return static_cast<Context*>(p)->record(4,0);};
    io.diagnostics.log=[](void* p,ConsoleLevel,const char*,const char*,std::uint32_t line,const char*){return static_cast<Context*>(p)->record(5,line);};
    if(missing==1)io.wifi_status=nullptr;
    if(missing==2)io.ethernet_acquire=nullptr;
    if(missing==3)io.ethernet_connect=nullptr;
    if(missing==4)io.ethernet_stop=nullptr;
    if(missing==5)io.diagnostics.log=nullptr;
    bool connected=true;words[6]=static_cast<std::uint32_t>(check_network_connectivity(words[0],io,connected));words[5]=connected;return c.count;
}
