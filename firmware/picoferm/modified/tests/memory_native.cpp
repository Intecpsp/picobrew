// Actual Controller + Engine + VerifiedStorage + pinned SPIFFS/UMM execution.
// The flash bytes, clock, ADC and network services are explicit test ports.
// A separate reservation represents SDK/network memory, not simulated Wi-Fi.
#include "picoferm/standalone.hpp"
#include "picobrew/provisioning.hpp"
#include "storage.hpp"
#include "spiffs/spiffs.h"
#include "umm_malloc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace api=picobrew::standalone;
namespace ferm=picoferm::standalone;
namespace sdk=picobrew::standalone::esp8266;
extern "C" {
alignas(8) unsigned char firmware_memory_heap[81920];
size_t firmware_memory_heap_size=0;
void* __real_malloc(size_t);void* __real_calloc(size_t,size_t);
void* __real_realloc(void*,size_t);void __real_free(void*);
std::uint32_t _SPIFFS_start=0,_SPIFFS_end=0,_SPIFFS_page=0,_SPIFFS_block=0;
}
namespace {
bool tracking=false;
unsigned checks=0,failures=0,allocations=0,peak=0,largest=0;
unsigned current_phase=0,peak_phase=0;
unsigned failed_request=0,failed_free=0,failed_contiguous=0;
unsigned phase_peaks[6]={};
unsigned phase_largest[6]={};
bool owned(const void* p){return reinterpret_cast<std::uintptr_t>(p)>=reinterpret_cast<std::uintptr_t>(firmware_memory_heap)&&
    reinterpret_cast<std::uintptr_t>(p)<reinterpret_cast<std::uintptr_t>(firmware_memory_heap)+firmware_memory_heap_size;}
void observe(size_t requested,void* result){
    ++allocations;if(!result&&requested){++failures;failed_request=requested;failed_free=umm_free_heap_size();failed_contiguous=ummHeapInfo.maxFreeContiguousBlocks*8;}
    if(requested>largest)largest=requested;
    if(requested>phase_largest[current_phase])phase_largest[current_phase]=requested;
    const unsigned used=firmware_memory_heap_size-umm_free_heap_size();
    if(used>peak){peak=used;peak_phase=current_phase;}
    if(used>phase_peaks[current_phase])phase_peaks[current_phase]=used;
}
void require(bool valid,const char* message){++checks;if(!valid){tracking=false;std::fprintf(stderr,"Ferm memory replay: %s (peak=%u failures=%u request=%u free=%u contiguous=%u)\n",message,peak,failures,failed_request,failed_free,failed_contiguous);std::exit(1);}}
constexpr unsigned FlashBytes=0xfb000,FlashBase=0x100000;
std::uint8_t flash[FlashBytes];
std::uint32_t clock_ms=0xffff0000u;
char arena[PICO_HTTP_ARENA_CAPACITY];
unsigned hal_reads=0,hal_writes=0;
bool range(unsigned address,unsigned size){return address>=FlashBase&&address-FlashBase<=FlashBytes&&size<=FlashBytes-(address-FlashBase);}
}
extern "C" void* __wrap_malloc(size_t size){if(!tracking)return __real_malloc(size);void* result=umm_malloc(size);observe(size,result);return result;}
extern "C" void* __wrap_calloc(size_t count,size_t size){if(!tracking)return __real_calloc(count,size);void* result=umm_calloc(count,size);observe(count*size,result);return result;}
extern "C" void* __wrap_realloc(void* old,size_t size){if(!owned(old)&&(!tracking||old))return __real_realloc(old,size);void* result=umm_realloc(old,size);observe(size,result);return result;}
extern "C" void __wrap_free(void* old){if(owned(old))umm_free(old);else __real_free(old);}
void* operator new(size_t size){void* p=__wrap_malloc(size);if(!p)std::abort();return p;}
void* operator new[](size_t size){return ::operator new(size);}
void* operator new(size_t size,const std::nothrow_t&) noexcept{return __wrap_malloc(size);}
void* operator new[](size_t size,const std::nothrow_t&) noexcept{return __wrap_malloc(size);}
void operator delete(void* p) noexcept{__wrap_free(p);}
void operator delete[](void* p) noexcept{__wrap_free(p);}
void operator delete(void* p,const std::nothrow_t&) noexcept{__wrap_free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept{__wrap_free(p);}
std::int32_t spiffs_hal_read(std::uint32_t address,std::uint32_t size,std::uint8_t* out){
    ++hal_reads;if(!range(address,size))return SPIFFS_ERR_INTERNAL;std::memcpy(out,flash+address-FlashBase,size);return SPIFFS_OK;}
std::int32_t spiffs_hal_write(std::uint32_t address,std::uint32_t size,std::uint8_t* in){
    ++hal_writes;if(!range(address,size))return SPIFFS_ERR_INTERNAL;for(unsigned i=0;i<size;++i)flash[address-FlashBase+i]&=in[i];return SPIFFS_OK;}
std::int32_t spiffs_hal_erase(std::uint32_t address,std::uint32_t size){
    if(!range(address,size)||address%4096||size%4096)return SPIFFS_ERR_INTERNAL;
    std::memset(flash+address-FlashBase,255,size);return SPIFFS_OK;}
namespace {
struct Bus {
    std::uint8_t registers[32]={};
    ferm::SensorIo binding(){ferm::SensorIo io;io.context=this;
        io.begin=[](void*,unsigned,unsigned,unsigned){return true;};
        io.read=[](void* p,std::uint8_t reg,std::uint8_t& value){auto& self=*static_cast<Bus*>(p);if(reg>=32)return false;value=self.registers[reg];
            if(reg==0)value=static_cast<std::uint8_t>(value|40u);
            if(reg==2)value&=static_cast<std::uint8_t>(~4u);
            return true;};
        io.write=[](void* p,std::uint8_t reg,std::uint8_t value){if(reg>=32)return false;static_cast<Bus*>(p)->registers[reg]=value;return true;};
        io.read24=[](void* p,std::uint8_t,std::uint32_t& value){value=(static_cast<Bus*>(p)->registers[2]&128u)?0x800000:0x7fffff;return true;};
        io.battery=[](void*,float& value){value=3.14f;return true;};return io;}
};
api::Clock clock_port(){api::Clock value;value.millis=[](void*){return clock_ms;};return value;}
sdk::SpiffsGeometry geometry(){sdk::SpiffsGeometry g;g.start=FlashBase;g.size=FlashBytes;g.page_size=256;g.block_size=8192;return g;}
unsigned request(api::Engine& engine,PicoHttpMethod method,const char* path,const char* body=nullptr){
    PicoHttpRequest request={};PicoHttpResponse response={};request.method=method;request.auth_configured=request.authenticated=true;
    std::snprintf(request.path,sizeof(request.path),"%s",path);request.body=body;request.body_length=body?std::strlen(body):0;
    require(engine.handle(request,response,arena,sizeof(arena)),"actual API handler");return response.status;}
void advance(ferm::Controller& controller,api::Engine& engine,unsigned count=500){
    for(unsigned n=0;n<count;++n){controller.poll();engine.poll();clock_ms+=5;}}
unsigned read_number(const char* name){char key[64];std::snprintf(key,sizeof(key),"\"%s\":",name);const char* value=std::strstr(arena,key);
    require(value!=nullptr,"response numeric field");return std::strtoul(value+std::strlen(key),nullptr,10);}
void format_fixture(){
    std::memset(flash,255,sizeof(flash));spiffs fs={};spiffs_config cfg={};
    cfg.phys_addr=FlashBase;cfg.phys_size=FlashBytes;cfg.phys_erase_block=4096;cfg.log_page_size=256;cfg.log_block_size=8192;
    cfg.hal_read_f=spiffs_hal_read;cfg.hal_write_f=spiffs_hal_write;cfg.hal_erase_f=spiffs_hal_erase;
    std::uint8_t work[512],descriptors[256],cache[1024];
    (void)SPIFFS_mount(&fs,&cfg,work,descriptors,sizeof(descriptors),cache,sizeof(cache),nullptr);SPIFFS_unmount(&fs);
    require(SPIFFS_format(&fs)==SPIFFS_OK,"format only test-owned flash before production mount");}
}
int main(int argc,char** argv){
    require(argc==4,"heap bytes, SDK/network and target platform reservations required");
    firmware_memory_heap_size=std::strtoul(argv[1],nullptr,10);const unsigned reserve=std::strtoul(argv[2],nullptr,10);
    const unsigned platform_bytes=std::strtoul(argv[3],nullptr,10);
    require(firmware_memory_heap_size<=sizeof(firmware_memory_heap)&&reserve<firmware_memory_heap_size,"bounded test heap");
    std::setvbuf(stdout,nullptr,_IONBF,0);format_fixture();umm_init();tracking=true;
    void* reservations[16]={};require(reserve%16==0,"split SDK/network reservation");
    for(void*& p:reservations){p=reserve?__wrap_malloc(reserve/16):nullptr;require(!reserve||p!=nullptr,"SDK/network reservation fits");}
    // Target owners outside the portable replay: AsyncTelemetry Impl, raw HTTP
    // listener Impl, EEPROM read buffer and UART receive buffer. Exact sizes
    // are cross-checked by pinned target probes in the orchestration script.
    void* platform=__wrap_malloc(platform_bytes);require(platform!=nullptr,"remaining platform owners fit");
    unsigned mounted=0,begun=0,active=0,frame=0;unsigned final_id=0;
    {
        sdk::SpiffsStorage storage(geometry());require(storage.begin(),"production SPIFFS mount");mounted=firmware_memory_heap_size-umm_free_heap_size();
        Bus bus;ferm::Controller controller(bus.binding(),storage.binding(),clock_port());
        api::Engine engine(controller.services(),storage.binding(),clock_port());controller.bind(engine);require(controller.identity("picoferm-memory-replay"),"UID");
        picoferm::oem::PersistentData original;original.word(original.cookie_position,0xdeadbeefu);original.word(original.version_position,1);
        original.word(original.slope_position,110840);original.word(original.pressure_offset_position,100000);original.bytes[original.has_offset_position]=1;
        require(controller.begin(original.bytes.data(),original.bytes.size()).ok(),"actual calibrated controller begin");
        api::Provisioning provision(storage.binding(),nullptr,[](void*){return true;});require(provision.load().ok(),"provisioner load");
        const char key[]="key 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        require(provision.execute(key,sizeof(key)-1).ok(),"verified key provisioning");
        require(engine.begin().ok(),"actual Engine.begin");begun=firmware_memory_heap_size-umm_free_heap_size();
        api::TelemetryIo io;io.begin=[](void*,const char*,std::uint16_t){return true;};io.poll_connect=[](void*){return 1;};
        io.send=[](void*,const std::uint8_t*,std::size_t)->int{return PICO_HTTP_IO_AGAIN;};io.receive=[](void*,std::uint8_t*,std::size_t)->int{return PICO_HTTP_IO_AGAIN;};io.close=[](void*){};
        engine.set_telemetry_io(io);advance(controller,engine);
        char host[128],forwarding[192];std::memset(host,'a',sizeof(host));host[63]='.';host[127]=0;
        std::snprintf(forwarding,sizeof(forwarding),"{\"enabled\":true,\"host\":\"%s\",\"port\":65535}",host);
        require(request(engine,PICO_HTTP_PUT,"/api/v1/telemetry",forwarding)==200,"enable maximum-length forwarding host");
        current_phase=1;engine.poll();frame=firmware_memory_heap_size-umm_free_heap_size();
        for(unsigned session=1;session<=9;++session){
            current_phase=2;require(request(engine,PICO_HTTP_POST,"/api/v1/commands","{\"command\":\"start\"}")==202,"reserve and start actual controller session");
            engine.poll();active=firmware_memory_heap_size-umm_free_heap_size();advance(controller,engine);
            for(unsigned sample=0;sample<29;++sample){
                current_phase=3;clock_ms+=300000;controller.poll();engine.poll();advance(controller,engine);
                current_phase=4;require(request(engine,PICO_HTTP_GET,"/api/v1/history")==200,"active catalogue during telemetry");
                final_id=read_number("id");char path[80];std::snprintf(path,sizeof(path),"/api/v1/history/samples/0?id=%u",final_id);
                require(request(engine,PICO_HTTP_GET,path)==200,"active sample page during telemetry");
                if(session>1){std::snprintf(path,sizeof(path),"/api/v1/history/samples/0?id=%u",session-1);
                    require(request(engine,PICO_HTTP_GET,path)==200,"retained sample page while another schema records");}
            }
            current_phase=5;require(request(engine,PICO_HTTP_POST,"/api/v1/commands","{\"command\":\"stop\"}")==202,"stop remains available");engine.poll();
            advance(controller,engine);
        }
        require(engine.persistence_ready(),"all durable operations retained readiness");
    }
    __wrap_free(platform);for(void* p:reservations)__wrap_free(p);
    require(!failures,"no allocation failed");require(final_id==9,"all nine sessions completed");
    const unsigned final_free=umm_free_heap_size();require(final_free==firmware_memory_heap_size-8,"all allocations return to the SDK free list");tracking=false;
    std::printf("{\"checks\":%u,\"pointer_bytes\":%zu,\"heap_bytes\":%zu,\"sdk_network_reservation\":%u,\"platform_reservation\":%u,\"allocations\":%u,\"allocation_failures\":%u,\"peak_used_bytes\":%u,\"minimum_free_bytes\":%zu,\"largest_request_bytes\":%u,\"peak_phase\":%u,\"phase_peaks\":[%u,%u,%u,%u,%u,%u],\"phase_largest_requests\":[%u,%u,%u,%u,%u,%u],\"mounted_bytes\":%u,\"engine_begin_bytes\":%u,\"active_bytes\":%u,\"telemetry_bytes\":%u,\"hal_reads\":%u,\"hal_writes\":%u,\"completed_sessions\":%u,\"final_free_bytes\":%u,\"passed\":true}\n",
        checks,sizeof(void*),firmware_memory_heap_size,reserve,platform_bytes,allocations,failures,peak,firmware_memory_heap_size-peak,largest,peak_phase,
        phase_peaks[0],phase_peaks[1],phase_peaks[2],phase_peaks[3],phase_peaks[4],phase_peaks[5],
        phase_largest[0],phase_largest[1],phase_largest[2],phase_largest[3],phase_largest[4],phase_largest[5],mounted,begun,active,frame,hal_reads,hal_writes,final_id,final_free);
}
