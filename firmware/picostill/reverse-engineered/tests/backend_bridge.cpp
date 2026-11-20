#include "controller_bridge.cpp"
#include "picostill/backend.hpp"
#include <stdexcept>
namespace {
struct Runtime final:HttpRuntime {
    unsigned tick=1;
    std::uint32_t millis() override{return ports.millis();}
    void cooperate() override{ports.events.push_back("yield");ports.clock+=tick;}
    void variable(const char* scope,const char* name,const char* value) override{ports.variable(scope,name,value);}
    void integer(const char* scope,const char* name,std::uint32_t value) override{ports.events.push_back(std::string("integer:")+scope+":"+name+":"+std::to_string(value));}
    void message(const char* value) override{ports.message(value);}
} runtime;
struct Network final:OutboundClient {
    const std::uint32_t* items=nullptr;unsigned size=0,position=0;bool connect=true,keep=true;std::string output;
    bool connect_ip(std::uint32_t address,unsigned port) override{ports.events.push_back("connect:"+std::to_string(address)+":"+std::to_string(port));return connect;}
    bool connect_host(const char*,unsigned) override{throw std::logic_error("host");}
    int available() override{ports.events.push_back("available:"+std::to_string(position<size));return position<size?1:0;}
    int read() override{const auto value=items[position++];ports.events.push_back("read:"+std::to_string(value));return static_cast<std::int32_t>(value);}
    bool connected() override{ports.events.push_back("connected:"+std::to_string(keep));return keep;}
    void write(const char* bytes,std::size_t size) override{const std::string text(bytes,size);output+=text;ports.events.push_back("write:"+text);}
    std::string read_line() override{throw std::logic_error("line");}
    std::size_t read_bytes(std::uint8_t*,std::size_t) override{throw std::logic_error("read_bytes");}
    void stop() override{throw std::logic_error("stop");}
    void flush() override{throw std::logic_error("flush");}
} network;
std::string backend_error;
}
extern "C" {
void backend_run(unsigned op,std::uint8_t* b,const char* const* strings,const std::uint32_t* items,unsigned size,unsigned command,unsigned now,unsigned tick,int connect,int connected){
    ports.events.clear();ports.clock=now;diagnostics.assertion_count=0;runtime.tick=tick;network.items=items;network.size=size;network.position=0;network.connect=connect;network.keep=connected;network.output.clear();
    BackendStatus status;status.done=read(b,0x3bc,1);status.has_update_address=read(b,0x3be,1);status.host_ip=read(b,0x3b8);DeviceData data;std::memcpy(data.uid.data(),strings[3],std::min<std::size_t>(15,std::strlen(strings[3])));
    Backend backend(status,data,network,runtime,diagnostics);backend.timeout=read(b,0);backend.pending=read(b,0x3c,1);backend.reading=read(b,0x3d,1);backend.deadline=read(b,0x40);backend.inside=read(b,0x44,1);backend.received=read(b,0x148);backend.pending_command=read(b,0x158);
    std::memcpy(backend.response.data(),b+0x45,256);std::memcpy(backend.request.data(),b+0x164,512);std::memcpy(backend.extra_headers.data(),b+0x36c,64);std::memcpy(backend.update_address.data(),b+0x3be,128);backend.request_length=read(b,0x364);backend.request_remaining=read(b,0x368);backend.has_error=read(b,0x3bd,1);std::memcpy(backend.response.data(),strings[4],std::strlen(strings[4])+1);
    switch(op){case 0:backend.send(strings[0],strings[1],strings[2]);break;case 1:backend.sync(command,strings[0],strings[1],strings[2]);break;case 2:backend.complete();break;case 3:backend.poll();break;case 4:backend.sync(command,strings[0],strings[1],strings[2]);backend.poll();break;}
    write(b,0x3c,backend.pending,1);write(b,0x3d,backend.reading,1);write(b,0x40,backend.deadline);write(b,0x44,backend.inside,1);write(b,0x148,backend.received);write(b,0x14c,0);write(b,0x158,backend.pending_command);
    std::memcpy(b+0x45,backend.response.data(),256);std::memcpy(b+0x164,backend.request.data(),512);std::memcpy(b+0x3be,backend.update_address.data(),128);write(b,0x364,backend.request_length);write(b,0x368,backend.request_remaining);write(b,0x3bc,status.done,1);write(b,0x3bd,backend.has_error,1);
    backend_error=backend.error?backend.error:"";
}
const char* backend_error_text(){return backend_error.c_str();}
const char* backend_sent(){return network.output.c_str();}
}
