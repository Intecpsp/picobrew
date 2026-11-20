#include "records.hpp"
namespace picobrew { namespace standalone { namespace detail {
namespace {
constexpr std::size_t Header=24,Maximum=4096;
std::uint32_t crc(std::uint32_t value,const std::uint8_t* bytes,std::size_t length){for(std::size_t i=0;i<length;++i){value^=bytes[i];for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u&static_cast<std::uint32_t>(-static_cast<std::int32_t>(value&1)));}return value;}
std::uint32_t name_hash(const char* name){return ~crc(0xffffffffu,reinterpret_cast<const std::uint8_t*>(name),std::strlen(name));}
struct Bank {RecordResult result=RecordResult::Missing;std::uint32_t generation=0;std::size_t length=0;char path[24]={0};};
bool exact(Storage storage,const char* name,std::size_t offset,std::uint8_t* data,std::size_t length,std::size_t expected){std::size_t count=0,total=0;return storage.read&&storage.read(storage.context,name,offset,data,length,count,total)==StorageResult::Ok&&count==length&&total==expected;}
Bank inspect(Storage storage,const char* logical,unsigned index){
    Bank bank;::snprintf(bank.path,sizeof(bank.path),"%s.%c",logical,'a'+index);if(!storage.read){bank.result=RecordResult::Io;return bank;}
    std::uint8_t header[Header];std::size_t read=0,total=0;auto result=storage.read(storage.context,bank.path,0,header,Header,read,total);
    if(result==StorageResult::Missing)return bank;
    if(result!=StorageResult::Ok){bank.result=RecordResult::Io;return bank;}
    bank.result=RecordResult::Corrupt;if(read!=Header||get32(header)!=0x31564250u||get32(header+4)!=1||get32(header+20)!=name_hash(logical))return bank;
    bank.length=get32(header+8);bank.generation=get32(header+12);if(!bank.generation||bank.length>Maximum||total!=Header+bank.length)return bank;
    std::uint32_t checksum=crc(0xffffffffu,header,16);checksum=crc(checksum,header+20,4);std::uint8_t chunk[128];
    for(std::size_t offset=0;offset<bank.length;){std::size_t count=bank.length-offset;if(count>sizeof(chunk))count=sizeof(chunk);if(!exact(storage,bank.path,Header+offset,chunk,count,total)){bank.result=RecordResult::Io;return bank;}checksum=crc(checksum,chunk,count);offset+=count;}
    if((~checksum)==get32(header+16))bank.result=RecordResult::Ok;
    return bank;
}
RecordResult select(Storage storage,const char* name,std::uint32_t expected,bool strict,Bank& selected){
    Bank a=inspect(storage,name,0),b=inspect(storage,name,1);if(a.result==RecordResult::Io||b.result==RecordResult::Io)return RecordResult::Io;
    if(strict&&(a.result==RecordResult::Corrupt||b.result==RecordResult::Corrupt))return RecordResult::Corrupt;
    const Bank* banks[2]={&a,&b};
    const Bank* best=nullptr;for(const Bank* bank:banks)if(bank->result==RecordResult::Ok&&(!expected||bank->generation==expected)&&(!best||static_cast<std::int32_t>(bank->generation-best->generation)>0))best=bank;
    if(!best)return a.result==RecordResult::Missing&&b.result==RecordResult::Missing&&!expected?RecordResult::Missing:RecordResult::Corrupt;
    selected=*best;return RecordResult::Ok;
}
}
RecordResult Records::load(const char* name,std::uint8_t* data,std::size_t capacity,std::size_t& length,std::uint32_t& generation,std::uint32_t expected,bool strict)const{
    length=0;generation=0;Bank best;const RecordResult result=select(storage_,name,expected,strict,best);if(result!=RecordResult::Ok)return result;
    if(!data&&!capacity){length=best.length;generation=best.generation;return RecordResult::Ok;}
    if(best.length>capacity||!data)return RecordResult::Corrupt;
    if(!exact(storage_,best.path,Header,data,best.length,Header+best.length))return RecordResult::Io;
    length=best.length;generation=best.generation;return RecordResult::Ok;
}
RecordResult Records::load_slice(const char* name,std::size_t offset,std::uint8_t* data,std::size_t length,std::size_t total,std::uint32_t generation)const{
    if(!generation||!data||offset>total||length>total-offset)return RecordResult::Corrupt;
    Bank best;const RecordResult result=select(storage_,name,generation,false,best);if(result!=RecordResult::Ok)return result;
    if(best.length!=total)return RecordResult::Corrupt;
    return exact(storage_,best.path,Header+offset,data,length,Header+total)?RecordResult::Ok:RecordResult::Io;
}
RecordResult Records::save(const char* name,const std::uint8_t* data,std::size_t length,std::uint32_t previous,std::uint32_t& generation)const{
    generation=previous;if(!storage_.write_atomic||length>Maximum||(!data&&length))return RecordResult::Io;
    std::uint32_t next=previous+1;if(!next)return RecordResult::Corrupt;Bytes bytes(Header+length);if(!bytes.data)return RecordResult::Io;
    put32(bytes.data,0x31564250u);put32(bytes.data+4,1);put32(bytes.data+8,length);put32(bytes.data+12,next);put32(bytes.data+20,name_hash(name));if(length)std::memcpy(bytes.data+Header,data,length);
    std::uint32_t checksum=crc(0xffffffffu,bytes.data,16);checksum=crc(checksum,bytes.data+20,4);checksum=crc(checksum,data,length);put32(bytes.data+16,~checksum);
    char path[24];::snprintf(path,sizeof(path),"%s.%c",name,'a'+((next-1)&1));if(storage_.write_atomic(storage_.context,path,bytes.data,bytes.size)!=StorageResult::Ok)return RecordResult::Io;
    const Bank bank=inspect(storage_,name,(next-1)&1);if(bank.result!=RecordResult::Ok||bank.generation!=next||bank.length!=length)return RecordResult::Io;
    // A matching checksum is followed by byte equality, including the header.
    std::uint8_t chunk[128];for(std::size_t offset=0;offset<bytes.size;){std::size_t count=bytes.size-offset;if(count>sizeof(chunk))count=sizeof(chunk);if(!exact(storage_,path,offset,chunk,count,bytes.size)||std::memcmp(chunk,bytes.data+offset,count))return RecordResult::Io;offset+=count;}
    generation=next;return RecordResult::Ok;
}
}}}
