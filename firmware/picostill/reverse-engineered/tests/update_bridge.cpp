#include "controller_bridge.cpp"
#include "picostill/update.hpp"
#include <algorithm>
#include <stdexcept>
namespace {
struct Runtime final:HttpRuntime {
    std::uint32_t millis() override{return ports.millis();}
    void cooperate() override{throw std::logic_error("yield");}
    void variable(const char* scope,const char* name,const char* value) override{ports.variable(scope,name,value);}
    void integer(const char* scope,const char* name,std::uint32_t value) override{ports.events.push_back(std::string("integer:")+scope+":"+name+":"+std::to_string(value));}
    void message(const char* value) override{ports.message(value);}
} runtime;
struct Network final:OutboundClient {
    const std::uint8_t* body=nullptr;const char* const* lines=nullptr;unsigned size=0,position=0,line=0,line_count=0,read_limit=2048;bool connect=true;std::string output;
    bool connect_ip(std::uint32_t,unsigned) override{throw std::logic_error("ip");}
    bool connect_host(const char* host,unsigned port) override{ports.events.push_back(std::string("connect_host:")+host+":"+std::to_string(port));return connect;}
    int available() override{const int count=line<line_count?1:size-position;ports.events.push_back("available:"+std::to_string(count));return count;}
    int read() override{throw std::logic_error("read");}
    bool connected() override{throw std::logic_error("connected");}
    void write(const char* bytes,std::size_t size) override{const std::string text(bytes,size);output+=text;ports.events.push_back("write:"+text);}
    std::string read_line() override{const std::string text=line<line_count?lines[line++]:"";ports.events.push_back("read_line:"+text);return text;}
    std::size_t read_bytes(std::uint8_t* out,std::size_t requested) override{const auto count=std::min<std::size_t>(requested,std::min(size-position,read_limit));std::memcpy(out,body+position,count);position+=count;ports.events.push_back("read_bytes:"+std::to_string(requested)+":"+std::to_string(count));return count;}
    void stop() override{ports.events.push_back("stop");}
    void flush() override{ports.events.push_back("flush");}
} network;
struct Flash final:UpdatePorts {
    const std::uint32_t* args=nullptr;
    bool begin(std::uint32_t size) override{ports.events.push_back("begin:"+std::to_string(size));return args[7];}
    std::size_t write(std::uint8_t* bytes,std::size_t size) override{ports.events.push_back("flash_write:"+hex(bytes,size));return std::min<std::size_t>(size,args[12]);}
    bool end() override{ports.events.push_back("end");return args[8];}
    bool finished() override{return args[9];}
    unsigned error() override{return args[10];}
    std::string md5() override{ports.events.push_back("md5");return "0123456789abcdef0123456789abcdef";}
    void console(const char* message,bool line) override{ports.events.push_back(std::string(line?"console_line:":"console:")+message);}
} flash;
}
extern "C" {
void update_run(unsigned op,std::uint32_t* args,const char* uri,const char* const* lines,unsigned line_count,const std::uint8_t* body,unsigned body_size,std::uint8_t* buffer,std::uint8_t* params){
    ports.events.clear();ports.clock=args[4];diagnostics.assertion_count=0;network.body=body;network.size=body_size;network.lines=lines;network.line_count=line_count;network.line=network.position=0;network.read_limit=args[11];network.connect=args[6];network.output.clear();flash.args=args;
    FirmwareUpdate update(network,flash,runtime,diagnostics);update.state=args[0];update.content_length=static_cast<std::int32_t>(args[1]);update.binary_content=args[2];update.written=args[3];update.changed_at=args[5];update.buffer.fill(0x5a);
    if(op==0){UpdateParameters p;p.parse(uri,diagnostics);p.report(runtime);std::memcpy(params,p.host.data(),64);write(params,64,p.host_length);write(params,68,p.host_remaining);std::memcpy(params+72,p.path.data(),64);write(params,136,p.path_length);write(params,140,p.path_remaining);params[144]=p.valid;}
    else if(op==1)update.begin("host","/path.bin");
    else if(op==2)update.poll();
    else{update.begin("host","/path.bin");for(unsigned i=0;i<args[13];++i)update.poll();}
    args[0]=update.state;args[1]=update.changed_at;args[2]=update.written;args[3]=static_cast<std::uint32_t>(update.content_length);args[4]=update.binary_content;std::memcpy(buffer,update.buffer.data(),2048);
}
const char* update_sent(){return network.output.c_str();}
}
