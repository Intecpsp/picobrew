#include "controller_bridge.cpp"
#include "picostill/storage.hpp"
#include <map>
#include <cstdlib>
namespace {
struct Storage final:StoragePorts {
    std::vector<std::uint8_t> blob;bool missing=false;
    int opened=0,query=0,read_error=0,write_error=0,expand=0,allocation=0,alloc_count=0;
    std::map<std::uint8_t*,unsigned> allocations;
    int open(const char* name,std::uint32_t& handle) override{ports.events.push_back(std::string("open:")+name);if(!opened)handle=7;return opened;}
    int get_blob(std::uint32_t handle,const char* key,void* data,std::size_t& size) override {
        ports.events.push_back("get:"+std::to_string(handle)+":"+key+":"+(data?"data":"size")+":"+std::to_string(size));
        const int failure=data?read_error:query;if(failure)return failure;if(missing)return 0x1102;
        if(data){if(size<blob.size())return 0x110c;std::memcpy(data,blob.data(),blob.size());}size=blob.size();return 0;
    }
    int set_blob(std::uint32_t handle,const char* key,const void* data,std::size_t size) override {
        ports.events.push_back("set:"+std::to_string(handle)+":"+key+":"+hex(data,size));
        const bool temp=std::string(key)=="expand";const int result=temp?expand:write_error;
        if(!result && !temp){const auto* b=static_cast<const std::uint8_t*>(data);blob.assign(b,b+size);missing=false;}return result;
    }
    int erase_key(std::uint32_t handle,const char* key) override {
        ports.events.push_back("erase:"+std::to_string(handle)+":"+key);if(std::string(key)=="eeprom"){blob.clear();missing=true;}return 0;
    }
    int commit(std::uint32_t handle) override{ports.events.push_back("commit:"+std::to_string(handle));return 0;}
    std::uint8_t* allocate(std::size_t size) override {
        ++alloc_count;ports.events.push_back("allocate:"+std::to_string(size)+":"+std::to_string(alloc_count));
        if(alloc_count==allocation)return nullptr;
        auto* b=static_cast<std::uint8_t*>(std::malloc(size));std::memset(b,0xa5,size);allocations[b]=alloc_count;return b;
    }
    void release(std::uint8_t* bytes,bool array_delete) override {
        ports.events.push_back("release:"+std::to_string(allocations.at(bytes))+":"+std::to_string(array_delete));allocations.erase(bytes);std::free(bytes);
    }
    void error(unsigned line,int status,std::uint32_t old_size,std::uint32_t new_size) override{
        ports.events.push_back("storage_error:"+std::to_string(line)+":"+std::to_string(status)+":"+std::to_string(old_size)+":"+std::to_string(new_size));
    }
    void key_size(std::uint32_t size) override{ports.events.push_back("key_size:"+std::to_string(size));}
    void ready() override{ports.events.push_back("variable:EEPROM:BEGIN:EEPROM finished");}
};
std::string stored_hex,buffer_hex,record_hex;std::vector<std::string> captured_events;
unsigned storage_result,storage_size,storage_dirty,storage_handle;bool storage_missing;
}
extern "C" {
void storage_run(unsigned op,unsigned requested,int blob_size,unsigned size,int present,int dirty,unsigned version,
                 int opened,int query,int read_failure,int write_failure,int expand,int allocation){
    ports.events.clear();ports.clock=1000;diagnostics.assertion_count=0;Storage nvs;nvs.opened=opened;nvs.query=query;nvs.read_error=read_failure;
    nvs.write_error=write_failure;nvs.expand=expand;nvs.allocation=allocation;nvs.missing=blob_size<0;
    if(blob_size>=0){nvs.blob.resize(blob_size);for(unsigned i=0;i<nvs.blob.size();++i)nvs.blob[i]=(i*17+3)&255;}
    CredentialStorage store(nvs,diagnostics);store.requested_size=requested;store.size=size;store.dirty=dirty;store.handle=9;
    if(present){store.buffer=static_cast<std::uint8_t*>(std::malloc(size?size:1));nvs.allocations[store.buffer]=0;for(unsigned i=0;i<size;++i)store.buffer[i]=(i*13+5)&255;}
    EepromData record;auto* b=reinterpret_cast<std::uint8_t*>(&record);for(unsigned i=0;i<sizeof(record);++i)b[i]=(i*7+2)&255;record.version=version;
    storage_result=0;if(op==0)storage_result=store.begin(requested);else if(op==1)store.initialize();else if(op==2)store.load(record);else store.save(record);
    storage_size=store.size;storage_handle=store.handle;storage_dirty=store.dirty;storage_missing=nvs.missing;
    stored_hex=hex(nvs.blob.data(),nvs.blob.size());buffer_hex=store.buffer?hex(store.buffer,store.size):"";record_hex=hex(&record,sizeof(record));captured_events=ports.events;
}
unsigned storage_value(unsigned i){return i==0?storage_result:i==1?storage_size:i==2?storage_handle:i==3?storage_dirty:storage_missing;}
const char* storage_bytes(unsigned i){return i==0?stored_hex.c_str():i==1?buffer_hex.c_str():record_hex.c_str();}
unsigned storage_event_count(){return captured_events.size();}
const char* storage_event(unsigned i){return captured_events.at(i).c_str();}
}
