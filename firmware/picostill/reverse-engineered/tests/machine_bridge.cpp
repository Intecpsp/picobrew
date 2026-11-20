#include "data_bridge.cpp"
#include "picostill/machine.hpp"
#include <cstdlib>
namespace {
struct MachineRuntime final:HttpRuntime {
    std::uint32_t millis() override{return ports.millis();}
    void cooperate() override{ports.events.push_back("yield");}
    void variable(const char* scope,const char* key,const char* value) override{ports.variable(scope,key,value);}
    void integer(const char* scope,const char* key,std::uint32_t value) override{ports.events.push_back(std::string("integer:")+scope+":"+key+":"+std::to_string(value));}
    void message(const char* text) override{ports.message(text);}
} machine_runtime;
struct MachineMemory final:StoragePorts {
    std::array<std::uint8_t,512> bytes={{0}};
    int open(const char*,std::uint32_t& handle) override{handle=42;return 0;}
    int get_blob(std::uint32_t,const char*,void* out,std::size_t& size) override{if(out)std::memcpy(out,bytes.data(),std::min(size,bytes.size()));size=bytes.size();return 0;}
    int set_blob(std::uint32_t,const char*,const void* in,std::size_t size) override{std::memcpy(bytes.data(),in,std::min(size,bytes.size()));return 0;}
    int erase_key(std::uint32_t,const char*) override{return 0;}
    int commit(std::uint32_t) override{return 0;}
    std::uint8_t* allocate(std::size_t size) override{return static_cast<std::uint8_t*>(std::calloc(size,1));}
    void release(std::uint8_t* data,bool) override{std::free(data);}
    void error(unsigned,int,std::uint32_t,std::uint32_t) override{throw std::logic_error("unexpected NVS error");}
    void key_size(std::uint32_t) override{}
    void ready() override{}
} memory;
struct MachineSystem final:SystemPorts,ReportPorts {
    const std::uint8_t* input=nullptr;unsigned input_size=0,position=0;std::uint64_t mac=0;bool configured=false,inspected=false;
    void inspect_partitions() override{ports.events.push_back("partition_configured");ports.events.push_back("partition_running");ports.events.push_back("partition_factory");inspected=true;}
    void report_partitions() override{ports.events.push_back("partition:FACTORY:"+std::to_string(inspected?0x3ff05000u:0u));ports.events.push_back("partition:RUNNING:"+std::to_string(inspected?0x3ff05040u:0u));if(inspected&&configured){ports.variable("SYS","PART","Not running the configured partition");ports.events.push_back("partition:CONFIGURED:"+std::to_string(0x3ff05080u));}}
    void initialize_hardware(Hardware&) override{ports.events.push_back("initialize_hardware");}
    void initialize_wifi() override{ports.events.push_back("persistent:0");ports.events.push_back("mode:0");ports.events.push_back("auto_reconnect:0");}
    std::uint64_t efuse_mac() override{ports.events.push_back("mac");return mac;}
    void credential_variable(const char* scope,const char* name,const char* value,bool mask) override{if(mask)ports.events.push_back(std::string("masked:")+scope+":"+name+":"+value);else ports.variable(scope,name,value);}
    int serial_available() override{ports.events.push_back("serial_available:"+std::to_string(input_size-position));return input_size-position;}
    int serial_read() override{const auto value=input[position++];ports.events.push_back("serial_read:"+std::to_string(value));return value;}
    void erase_nvs() override{throw std::logic_error("NVS erase outside integration case");}
    void select_factory_partition() override{throw std::logic_error("factory select outside integration case");}
    void deep_sleep(std::uint64_t) override{throw std::logic_error("sleep outside integration case");}
    void restart() override{throw std::logic_error("restart outside integration case");}
    int scan_count() override{throw std::logic_error("scan outside integration case");}
    const char* scan_ssid(unsigned) override{throw std::logic_error("scan outside integration case");}
    void integer(const char* scope,const char* name,std::uint32_t value) override{machine_runtime.integer(scope,name,value);}
    void floating(const char* scope,const char* name,float value) override{char bits_text[9];snprintf(bits_text,sizeof(bits_text),"%08x",bits(value));ports.events.push_back(std::string("floating:")+scope+":"+name+":"+bits_text);}
    void format_decimal(double value,int width,unsigned precision,char* out,std::size_t capacity) override{formats.format_decimal(value,width,precision,out,capacity);}
    void console(const char* text) override{ports.events.push_back(std::string("console:")+text);}
} machine_system;
struct MachineTransport final:OutboundClient {
    bool connect_ip(std::uint32_t,unsigned) override{throw std::logic_error("connect");}
    bool connect_host(const char*,unsigned) override{throw std::logic_error("connect");}
    int available() override{throw std::logic_error("available");}
    int read() override{throw std::logic_error("read");}
    bool connected() override{throw std::logic_error("connected");}
    void write(const char*,std::size_t) override{throw std::logic_error("write");}
    std::string read_line() override{throw std::logic_error("line");}
    std::size_t read_bytes(std::uint8_t*,std::size_t) override{throw std::logic_error("read_bytes");}
    void stop() override{throw std::logic_error("stop");}
    void flush() override{throw std::logic_error("flush");}
} machine_transport;
struct MachineFlash final:UpdatePorts {
    bool begin(std::uint32_t) override{throw std::logic_error("begin");}
    std::size_t write(std::uint8_t*,std::size_t) override{throw std::logic_error("write");}
    bool end() override{throw std::logic_error("end");}
    bool finished() override{throw std::logic_error("finished");}
    unsigned error() override{throw std::logic_error("error");}
    std::string md5() override{throw std::logic_error("md5");}
    void console(const char*,bool) override{throw std::logic_error("console");}
} machine_flash;
struct MachineListener final:HttpListener {
    void listen(unsigned) override{throw std::logic_error("listen");}
    HttpStream* accept() override{throw std::logic_error("accept");}
    void release(HttpStream&) override{throw std::logic_error("release");}
} machine_listener;
std::uint32_t machine_assertions;
}
extern "C" {
void machine_run(unsigned op,std::uint8_t* b,const char* command,const std::uint8_t* serial,unsigned serial_size,const std::uint8_t* stored,std::uint64_t mac,unsigned configured,unsigned now){
    ports.clock=now;ports.high=true;ports.pressure={64,128,0,0};ports.adc={8,0,8,0,8,0,8,0};machine_system.mac=mac;machine_system.configured=configured;machine_system.inspected=false;machine_system.input=serial;machine_system.input_size=serial_size;machine_system.position=0;memory.bytes.fill(0);std::memcpy(memory.bytes.data(),stored,100);
    Machine machine(ports,ports,ports,memory,network,machine_runtime,machine_listener,machine_transport,machine_transport,machine_flash,machine_system,machine_system);
    load(machine.hardware,machine.controller,b+0x1a0,0x40);load_leds(machine.leds,b);load_app(machine.application,machine.wifi_status,machine.backend_status,b);std::memcpy(&machine.application.data,b+0x18,208);std::memcpy(machine.reports.csv.data(),b+0x678,128);machine.reports.length=read(b,0x6f8);machine.reports.remaining=read(b,0x6fc);
    std::memcpy(machine.receiving.data(),b+0x1448,128);machine.receiving_length=read(b,0x14c8);machine.receiving_remaining=read(b,0x14cc);std::memcpy(machine.queued.data(),b+0x14d0,128);machine.queued_length=read(b,0x1550);machine.queued_remaining=read(b,0x1554);
    ports.events.clear();machine.diagnostics.assertion_count=0;
    switch(op){case 0:machine.dispatch_console();break;case 1:machine.queue_console(command);break;case 2:machine.receive_console();break;case 3:machine.setup();break;case 4:machine.self_tests();break;}
    save(machine.hardware,machine.controller,b+0x1a0,0x40);save_leds(machine.leds,b);save_app(machine.application,machine.wifi_status,machine.backend_status,b);std::memcpy(b+0x18,&machine.application.data,208);std::memcpy(b+0x108,&machine.application.stored,100);std::memcpy(b+0x678,machine.reports.csv.data(),128);write(b,0x6f8,machine.reports.length);write(b,0x6fc,machine.reports.remaining);
    std::memcpy(b+0x1448,machine.receiving.data(),128);write(b,0x14c8,machine.receiving_length);write(b,0x14cc,machine.receiving_remaining);std::memcpy(b+0x14d0,machine.queued.data(),128);write(b,0x1550,machine.queued_length);write(b,0x1554,machine.queued_remaining);
    if(op==3){std::memcpy(b+0xfbc,machine.backend.extra_headers.data(),64);const auto n=std::strlen(machine.backend.extra_headers.data());write(b,0xffc,n);write(b,0x1000,63-n);write(b,0x190,0x3ff05000u);write(b,0x194,0x3ff05040u);write(b,0x198,configured?0x3ff05080u:0x3ff05040u);}
    machine_assertions=machine.diagnostics.assertion_count;
}
unsigned machine_assertion_count(){return machine_assertions;}
const std::uint8_t* machine_persisted(){return memory.bytes.data();}
}
