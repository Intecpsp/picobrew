#include "zseries/pinger.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
struct Context {std::uint32_t* words;std::uint32_t* effects;unsigned count=0;
    void event(std::uint32_t id,std::uint32_t a=0,std::uint32_t b=0){effects[count++]=id;effects[count++]=a;effects[count++]=b;}};
extern "C" unsigned pinger_case(std::uint32_t* words,std::uint32_t* effects){
    Context c;c.words=words;c.effects=effects;Pinger p;p.target=words[12];p.remaining=static_cast<std::int32_t>(words[2]);
    auto& s=p.services;s.context=&c;s.diagnostics.context=&c;
    s.wifi_status=[](void* v,std::int32_t& result){auto& c=*static_cast<Context*>(v);result=static_cast<std::int32_t>(c.words[3]);c.event(1,c.words[3]);return true;};
    s.set_option=[](void* v,std::uint32_t option,std::uint32_t value){static_cast<Context*>(v)->event(2,option,value);return true;};
    s.start=[](void* v,std::int32_t& error){auto& c=*static_cast<Context*>(v);error=static_cast<std::int32_t>(c.words[4]);c.event(3,c.words[4]);return true;};
    s.resolve=[](void* v,const char*,std::uint32_t& address,std::int32_t& result){auto& c=*static_cast<Context*>(v);address=c.words[6];result=static_cast<std::int32_t>(c.words[5]);c.event(4,c.words[5],address);return true;};
    s.parse=[](void* v,const char*,std::uint32_t& address){auto& c=*static_cast<Context*>(v);address=c.words[7];c.event(5,address);return true;};
    s.register_commands=[](void* v){static_cast<Context*>(v)->event(9);return true;};
    s.diagnostics.log=[](void* v,ConsoleLevel,const char* tag,const char* file,std::uint32_t line,const char*){
        assert(!std::strcmp(tag,"PING")&&!std::strcmp(file,"Pinger.h"));static_cast<Context*>(v)->event(6,line);return true;};
    s.diagnostics.assertion=[](void* v,const char* file,std::uint32_t line,const char*){auto& c=*static_cast<Context*>(v);
        assert(!std::strcmp(file,"Pinger.h"));c.event(7,line,c.words[9]);++c.words[9];c.event(8,200,c.words[9]);return true;};
    const char* text=words[8]==1?nullptr:words[8]==2?"":"example.test";bool started=false;PingerResult r=PingerResult::Ok;
    switch(words[0]){
    case 0:r=p.ping(words[1],started);break;
    case 1:r=p.hostname(text,started);break;
    case 2:r=p.address(text,started);break;
    case 3:{PingStatistics st;st.response_ms=words[15];st.timeouts=words[16];st.sent=words[17];st.received=words[18];
        st.errors=words[19];st.bytes=words[20];st.total_bytes=words[21];st.total_ms=words[22];st.minimum_ms=words[23];st.maximum_ms=words[24];r=p.receive(st);break;}
    case 4:r=p.setup();break;
    default:assert(false);
    }
    words[2]=static_cast<std::uint32_t>(p.remaining);words[10]=started;words[12]=p.target;words[13]=static_cast<std::uint32_t>(r);return c.count;
}
