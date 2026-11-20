#include "storage.hpp"
#include "spiffs/spiffs.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace picobrew::standalone;
using namespace picobrew::standalone::esp8266;
namespace {
#ifndef PICOBREW_SPIFFS_TEST_FLASH_SIZE
#define PICOBREW_SPIFFS_TEST_FLASH_SIZE 262144
#endif
constexpr std::uint32_t flash_start=0x100000,page_size=256;
std::uint32_t flash_size=PICOBREW_SPIFFS_TEST_FLASH_SIZE,block_size=8192;
std::vector<std::uint8_t> flash(flash_size,0xff);
unsigned checks=0,read_calls=0,write_calls=0,erase_calls=0;
bool read_failure=false,write_failure=false,erase_failure=false;
void require(bool ok,const char* message) {
    ++checks;if(!ok){std::fprintf(stderr,"raw SPIFFS adapter: %s\n",message);std::exit(1);}
}
bool range(std::uint32_t address,std::uint32_t size) {
    return address>=flash_start&&address-flash_start<=flash.size()&&size<=flash.size()-(address-flash_start);
}
SpiffsGeometry geometry() {
    SpiffsGeometry value;value.start=flash_start;value.size=flash_size;value.page_size=page_size;value.block_size=block_size;return value;
}
std::vector<std::uint8_t> payload(std::size_t size,unsigned seed) {
    std::vector<std::uint8_t> result(size);for(std::size_t n=0;n<size;++n)result[n]=static_cast<std::uint8_t>(n*43+seed);return result;
}
std::vector<std::uint8_t> read_all(Storage storage,const char* key,StorageResult& status) {
    std::size_t copied=99,total=99;status=storage.read(storage.context,key,0,nullptr,0,copied,total);
    require(!copied,"size query does not copy");if(status!=StorageResult::Ok)return {};
    std::vector<std::uint8_t> data(total);status=storage.read(storage.context,key,0,data.data(),data.size(),copied,total);
    if(status==StorageResult::Ok)require(copied==data.size(),"exact read count");
    return data;
}
}

extern "C" {std::uint32_t _SPIFFS_start=0,_SPIFFS_end=0,_SPIFFS_page=0,_SPIFFS_block=0;}
std::int32_t spiffs_hal_read(std::uint32_t address,std::uint32_t size,std::uint8_t* data) {
    ++read_calls;if(read_failure||!range(address,size))return SPIFFS_ERR_INTERNAL;
    std::memcpy(data,flash.data()+address-flash_start,size);return SPIFFS_OK;
}
std::int32_t spiffs_hal_write(std::uint32_t address,std::uint32_t size,std::uint8_t* data) {
    ++write_calls;if(write_failure||!range(address,size))return SPIFFS_ERR_INTERNAL;
    for(std::uint32_t n=0;n<size;++n)flash[address-flash_start+n]&=data[n];
    return SPIFFS_OK;
}
std::int32_t spiffs_hal_erase(std::uint32_t address,std::uint32_t size) {
    ++erase_calls;if(erase_failure||!range(address,size)||address%4096||size%4096)return SPIFFS_ERR_INTERNAL;
    std::fill(flash.begin()+address-flash_start,flash.begin()+address-flash_start+size,0xff);return SPIFFS_OK;
}

namespace {
struct Raw {
    spiffs fs;
    spiffs_config cfg;
    std::vector<std::uint8_t> work,descriptors,cache;
    Raw() {
        std::memset(&fs,0,sizeof(fs));std::memset(&cfg,0,sizeof(cfg));
        cfg.phys_addr=flash_start;cfg.phys_size=flash_size;cfg.phys_erase_block=4096;
        cfg.log_page_size=page_size;cfg.log_block_size=block_size;
        cfg.hal_read_f=spiffs_hal_read;cfg.hal_write_f=spiffs_hal_write;cfg.hal_erase_f=spiffs_hal_erase;
        fs.cfg.log_page_size=page_size;work.resize(page_size*2);
        descriptors.resize(SPIFFS_buffer_bytes_for_filedescs(&fs,3));cache.resize(SPIFFS_buffer_bytes_for_cache(&fs,3));
    }
    ~Raw(){SPIFFS_unmount(&fs);}
    s32_t mount(){return SPIFFS_mount(&fs,&cfg,work.data(),descriptors.data(),descriptors.size(),cache.data(),cache.size(),nullptr);}
    void format_fixture() {
        (void)mount();SPIFFS_unmount(&fs);require(SPIFFS_format(&fs)==SPIFFS_OK,"format test-owned blank flash");
        require(mount()==SPIFFS_OK,"mount formatted fixture");
    }
};
void mount_and_geometry() {
    const auto blank=flash;const unsigned writes=write_calls,erases=erase_calls;
    {SpiffsStorage storage(geometry());require(!storage.begin(),"blank partition is not autoformatted");
     require(!storage.mounted()&&storage.last_error()<0,"failed mount retained");
     StorageResult result;require(read_all(storage.binding(),"cfg.a",result).empty()&&result==StorageResult::Error,"unmounted store is error, not missing");}
    require(flash==blank&&write_calls==writes&&erase_calls==erases,"blank mount preserves every flash byte");
    for(unsigned which=0;which<7;++which){SpiffsGeometry g=geometry();
        if(which==0)g.size=0;
        if(which==1)g.page_size=0;
        if(which==2)g.page_size=257;
        if(which==3)g.block_size=0;
        if(which==4)g.start+=1;
        if(which==5)g.size=block_size;
        if(which==6)g.size-=1;
        SpiffsStorage storage(g);require(!storage.begin(),"invalid geometry rejected");}
    {Raw raw;raw.format_fixture();}
    const auto formatted=flash;read_failure=true;
    {SpiffsStorage storage(geometry());require(!storage.begin(),"flash read mount failure");}
    read_failure=false;require(flash==formatted,"failed read mount preserves flash");
}
void actual_filesystem() {
    const auto old=payload(8193,17),fresh=payload(9007,29);
    {SpiffsStorage storage(geometry());require(storage.begin()&&storage.begin(),"mount is idempotent");
     Storage io=storage.binding();StorageResult result;
     require(read_all(io,"cfg.a",result).empty()&&result==StorageResult::Missing,"actual SPIFFS absence");
     require(io.write_atomic(io.context,"cfg.a",old.data(),old.size())==StorageResult::Ok,"actual SPIFFS initial write");
     require(read_all(io,"cfg.a",result)==old&&result==StorageResult::Ok,"actual SPIFFS initial bytes");
     require(io.write_atomic(io.context,"ferm-cal",old.data(),24)==StorageResult::Ok,"actual PicoFerm calibration key");
     require(read_all(io,"ferm-cal",result)==std::vector<std::uint8_t>(old.begin(),old.begin()+24)&&result==StorageResult::Ok,"actual PicoFerm calibration readback");
     require(io.write_atomic(io.context,"z-recovery.a",old.data(),41)==StorageResult::Ok,"actual Z recovery bank name on shared transaction");
     require(read_all(io,"z-recovery.a",result)==std::vector<std::uint8_t>(old.begin(),old.begin()+41)&&result==StorageResult::Ok,"actual recovery bank readback");
     require(io.write_atomic(io.context,"cfg.a",fresh.data(),fresh.size())==StorageResult::Ok,"replacement despite non-overwriting rename");
     require(read_all(io,"cfg.a",result)==fresh&&result==StorageResult::Ok,"actual SPIFFS replacement bytes");
     std::uint8_t part[19];std::size_t copied=0,total=0;
     require(io.read(io.context,"cfg.a",4091,part,sizeof(part),copied,total)==StorageResult::Ok&&copied==sizeof(part)&&total==fresh.size(),"actual seek across page boundary");
     require(!std::memcmp(part,fresh.data()+4091,sizeof(part)),"actual offset bytes");
     require(io.write_atomic(io.context,"empty.a",nullptr,0)==StorageResult::Ok,"empty file creation");
     require(read_all(io,"empty.a",result).empty()&&result==StorageResult::Ok,"empty is not absent");
     require(io.remove(io.context,"empty.a")==StorageResult::Ok&&io.remove(io.context,"empty.a")==StorageResult::Missing,"actual remove statuses");
     storage.end();require(!storage.mounted(),"explicit unmount");}
    {SpiffsStorage storage(geometry());require(storage.begin(),"remount persisted replacement");StorageResult result;
     require(read_all(storage.binding(),"cfg.a",result)==fresh&&result==StorageResult::Ok,"persisted bytes after remount");}
    // Use the actual SDK to leave the transaction's interrupted rename state.
    {Raw raw;require(raw.mount()==SPIFFS_OK,"raw crash-state mount");
     require(SPIFFS_rename(&raw.fs,"/pb/cfg.a","/pb/cfg.a.bak")==SPIFFS_OK,"interrupt after preserving old main");}
    {SpiffsStorage storage(geometry());require(storage.begin(),"remount interrupted transaction");StorageResult result;Storage io=storage.binding();
     require(read_all(io,"cfg.a",result)==fresh&&result==StorageResult::Ok,"actual backup fallback");
     require(io.write_atomic(io.context,"cfg.a",old.data(),old.size())==StorageResult::Ok,"recover before replacement");
     require(read_all(io,"cfg.a",result)==old&&result==StorageResult::Ok,"recovered replacement");}
    {SpiffsStorage storage(geometry());require(storage.begin(),"mount write-failure fixture");Storage io=storage.binding();
     write_failure=true;require(io.write_atomic(io.context,"cfg.a",fresh.data(),fresh.size())==StorageResult::Error,"flash write failure propagated");
     write_failure=false;storage.end();require(storage.begin(),"remount after flash write failure");StorageResult result;
     require(read_all(io,"cfg.a",result)==old&&result==StorageResult::Ok,"old main preserved on write failure");
     read_failure=true;(void)read_all(io,"cfg.a",result);require(result==StorageResult::Error,"flash read failure is not missing");read_failure=false;}
    {SpiffsStorage storage(geometry());require(storage.begin(),"final mount");Storage io=storage.binding();
     require(io.write_atomic(io.context,"abcdefghijklmnopqrstuvw",fresh.data(),37)==StorageResult::Ok,"maximum logical name on actual SPIFFS");
     require(io.remove(io.context,"cfg.a")==StorageResult::Ok,"remove recovered main and backup");StorageResult result;
     require(read_all(io,"cfg.a",result).empty()&&result==StorageResult::Missing,"removed data cannot reappear");}
}
}
int main() {
    mount_and_geometry();actual_filesystem();
    std::printf("{\"checks\":%u,\"actual_spiffs_hal_reads\":%u,\"actual_spiffs_hal_writes\":%u,\"actual_spiffs_hal_erases\":%u,\"flash_bytes\":%zu,\"pointer_bytes\":%zu,\"passed\":true}\n",checks,read_calls,write_calls,erase_calls,flash.size(),sizeof(void*));
    return 0;
}
