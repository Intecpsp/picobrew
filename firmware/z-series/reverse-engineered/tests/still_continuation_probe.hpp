// Native callback recorder for complete PicoStill instruction comparisons.
#pragma once
#include "zseries/console.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
struct StillContinuationProbe {
    zseries::oem::ConsoleServices services;
    std::vector<std::string> trace;
    std::uint32_t clock=0,counter=0;
    StillContinuationProbe(){
        services.context=this;
        services.assertion=[](void* p,const char* file,std::uint32_t line,const char* expression){
            auto& self=*static_cast<StillContinuationProbe*>(p);
            self.trace.push_back(std::string("assertion|")+file+"|"+std::to_string(line)+"|"+expression);
            ++self.counter;self.clock+=200;return true;
        };
        services.log=[](void* p,zseries::oem::ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* message){
            static_cast<StillContinuationProbe*>(p)->trace.push_back("log|"+std::to_string(static_cast<unsigned>(level))+"|"+tag+"|"+file+"|"+std::to_string(line)+"|"+message);return true;
        };
    }
    void service(std::size_t index){trace.push_back("service|"+std::to_string(index));}
};
static StillContinuationProbe still_continuation_probe;
extern "C" void still_continuation_begin(std::uint32_t now,std::uint32_t counter){
    still_continuation_probe.trace.clear();still_continuation_probe.clock=now;still_continuation_probe.counter=counter;
}
extern "C" int still_continuation_output(char* data,std::size_t capacity,std::uint32_t* state){
    std::size_t count=0;for(const auto& row:still_continuation_probe.trace)count+=row.size()+1;
    if(count>capacity)return -1;
    std::size_t at=0;for(const auto& row:still_continuation_probe.trace){std::memcpy(data+at,row.c_str(),row.size()+1);at+=row.size()+1;}
    state[0]=still_continuation_probe.clock;state[1]=still_continuation_probe.counter;return static_cast<int>(count);
}
