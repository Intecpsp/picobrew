#include "data_bridge.cpp"
#include "picoferm/backend.hpp"
namespace {
struct FakeRuntime final:Runtime {FakeHardware& hardware;explicit FakeRuntime(FakeHardware& h):hardware(h){}void delay_ms(std::uint32_t value) override{ports.events.push_back("delay:"+std::to_string(value));hardware.now+=value;}};
struct FakeOutbound final:OutboundClient {
    std::vector<std::uint32_t> items,connections;unsigned position=0,attempt=0;bool alive=true;std::string sent;
    bool connect_ip(std::uint32_t address,unsigned port) override{ports.events.push_back("connect:"+std::to_string(address)+":"+std::to_string(port));const auto index=attempt<connections.size()?attempt:connections.size()-1;++attempt;return connections[index]!=0;}
    int available() override{const int value=position<items.size()?1:0;ports.events.push_back("available:"+std::to_string(value));return value;}
    int read() override{const auto value=items.at(position++);ports.events.push_back("read:"+std::to_string(value));return value;}
    bool connected() override{ports.events.push_back("connected:"+std::to_string(alive));return alive;}
    void write(const char* bytes,std::size_t length) override{const std::string text(bytes,length);sent+=text;ports.events.push_back("write:"+text);}
    void stop() override{ports.events.push_back("stop");}
};
std::uint32_t read_word(const std::uint8_t* p,unsigned o){return std::uint32_t(p[o])|std::uint32_t(p[o+1])<<8|std::uint32_t(p[o+2])<<16|std::uint32_t(p[o+3])<<24;}
void write_word(std::uint8_t* p,unsigned o,std::uint32_t value){for(unsigned i=0;i<4;++i)p[o+i]=value>>(i*8);}
void load_backend(Backend& b,const std::uint8_t* p,const char* error){
    b.timeout=read_word(p,0);b.pending=p[0x2c];b.reading=p[0x2d];b.deadline=read_word(p,0x30);b.inside=p[0x34];std::memcpy(b.response.data(),p+0x35,256);b.received=read_word(p,0x138);b.transport_error=read_word(p,0x13c)?error:nullptr;b.pending_command=read_word(p,0x148);
    std::memcpy(b.request.bytes.data(),p+0x15c,512);b.request.length=read_word(p,0x35c);b.request.left=read_word(p,0x360);
    std::memcpy(b.extra_headers.bytes.data(),p+0x364,64);b.extra_headers.length=read_word(p,0x3a4);b.extra_headers.left=read_word(p,0x3a8);
    b.send_logs=p[0x3ac];b.rate=read_word(p,0x3b0);b.voltage=number(read_word(p,0x3b4));b.host_ip=read_word(p,0x3bc);
    b.status.started=p[0x3c0];b.status.done=p[0x3c1];b.status.error=p[0x3c2];b.status.known=p[0x3c3];b.status.registered=p[0x3c4];b.status.needs_update=p[0x3c5];std::memcpy(b.status.update_address.data(),p+0x3c6,128);b.status.session.state=static_cast<SessionState>(read_word(p,0x448));b.status.session.error_flags=read_word(p,0x44c);b.status.data_sent=p[0x450];
}
void save_backend(const Backend& b,std::uint8_t* p){
    write_word(p,0,b.timeout);p[0x2c]=b.pending;p[0x2d]=b.reading;write_word(p,0x30,b.deadline);p[0x34]=b.inside;std::memcpy(p+0x35,b.response.data(),256);write_word(p,0x138,b.received);write_word(p,0x13c,0);write_word(p,0x148,b.pending_command);
    std::memcpy(p+0x15c,b.request.bytes.data(),512);write_word(p,0x35c,b.request.length);write_word(p,0x360,b.request.left);
    std::memcpy(p+0x364,b.extra_headers.bytes.data(),64);write_word(p,0x3a4,b.extra_headers.length);write_word(p,0x3a8,b.extra_headers.left);
    p[0x3ac]=b.send_logs;write_word(p,0x3b0,b.rate);write_word(p,0x3b4,bits(b.voltage));write_word(p,0x3bc,b.host_ip);
    p[0x3c0]=b.status.started;p[0x3c1]=b.status.done;p[0x3c2]=b.status.error;p[0x3c3]=b.status.known;p[0x3c4]=b.status.registered;p[0x3c5]=b.status.needs_update;std::memcpy(p+0x3c6,b.status.update_address.data(),128);write_word(p,0x448,static_cast<unsigned>(b.status.session.state));write_word(p,0x44c,b.status.session.error_flags);p[0x450]=b.status.data_sent;
}
std::string backend_error,backend_sent;std::uint32_t backend_clock=0,backend_position=0;
}
extern "C" {
void ferm_backend_run(unsigned op,std::uint8_t* bytes,const std::uint8_t* record,const std::uint32_t* parameters,const std::uint32_t* items,unsigned count,const std::uint32_t* connections,unsigned attempts,const char* text,const char* key,const char* value,const char* response,const char* error){
    ports.events.clear();FakeHardware hw;hw.now=parameters[0];hw.tick=parameters[1];FakeRuntime runtime(hw);FakeFormatter formatter;FakeOutbound client;client.items.assign(items,items+count);client.connections.assign(connections,connections+attempts);client.alive=parameters[5];
    DeviceData data;std::strcpy(data.uid.data(),"001122334455");std::strcpy(data.version.data(),"0.2.7");std::strcpy(data.token.data(),"12345678");PersistentData stored;std::memcpy(stored.bytes.data(),record,236);
    Backend backend(ports,hw,runtime,formatter,client,data,stored);load_backend(backend,bytes,error);std::strcpy(backend.response.data(),response);
    const auto sync=[&](){backend.sync(static_cast<std::int32_t>(parameters[2]),number(parameters[3]),parameters[4]);};
    switch(op){case 0:backend.set_headers(text);break;case 1:backend.request_prefix(text);break;case 2:backend.argument(key,value);break;case 3:backend.send(text,backend.extra_headers.c_str());break;case 4:backend.fail(text);break;case 5:sync();break;case 6:backend.request_state();break;case 7:backend.request_logs();break;case 8:backend.complete();break;case 9:backend.poll();break;case 10:sync();for(unsigned i=0;i<parameters[6];++i)backend.poll();break;}
    save_backend(backend,bytes);backend_error=backend.transport_error?backend.transport_error:"";backend_sent=client.sent;backend_clock=hw.now;backend_position=client.position;
}
const char* ferm_backend_error(){return backend_error.c_str();}const char* ferm_backend_sent(){return backend_sent.c_str();}
std::uint32_t ferm_backend_clock(){return backend_clock;}std::uint32_t ferm_backend_position(){return backend_position;}
}
