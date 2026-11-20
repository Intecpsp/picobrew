#include "zseries/local_http_state.hpp"
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
LocalHttpServer server;
std::vector<std::uint8_t> output;
std::uint32_t response[4];
bool headers(void*,std::uint32_t status,std::int32_t length,const char* mime) {
    response[0]=status;response[1]=static_cast<std::uint32_t>(length);
    return mime && std::strcmp(mime,"application/json")==0;
}
bool write(void*,const std::uint8_t* bytes,std::size_t size) { output.assign(bytes,bytes+size);return true; }
}
extern "C" {
int http_state_run(const std::uint64_t* numbers,const std::uint32_t* values,const char* name) {
    server=LocalHttpServer();output.clear();server.length=7;server.frozen=false;
    std::memcpy(server.buffer.data(),"earlier",8);
    server.services.response.headers=headers;server.services.response.write=write;
    LocalHttpState state;
    double* fields[]={&state.thermoblock_temperature,&state.wort_temperature,&state.ambient_temperature,
        &state.drain_temperature,&state.target_temperature};
    for(unsigned i=0;i<5;++i)std::memcpy(fields[i],numbers+i,8);
    state.valve_position=values[0];state.pumps=values[1];state.pause_reason=values[2];
    state.rssi=static_cast<std::int32_t>(values[3]);state.elapsed_seconds=values[4];
    state.remaining_seconds=values[5];state.runtime_flags=values[6];state.step_name=name;
    const auto result=send_local_http_state(server,state);
    response[2]=server.length;response[3]=server.frozen;
    return static_cast<int>(result);
}
const std::uint32_t* http_state_response(){return response;}
std::size_t http_state_output(std::uint8_t* bytes,std::size_t capacity){
    if(output.size()>capacity)return capacity+1;
    std::memcpy(bytes,output.data(),output.size());return output.size();
}
}
