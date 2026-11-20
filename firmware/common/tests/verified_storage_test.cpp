#include "picobrew/verified_storage.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace picobrew::standalone;
namespace {
unsigned checks = 0, failure_cases = 0, crash_states = 0;
void require(bool condition, const char* text) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "verified storage: %s\n", text); std::exit(1); }
}
using Bytes = std::vector<std::uint8_t>;
using Files = std::map<std::string, Bytes>;
struct Open { std::string name; std::size_t offset; bool writing; };
struct Memory {
    Files files;
    std::map<FileHandle, Open> opened;
    std::vector<Files> snapshots;
    std::vector<std::string> calls;
    std::size_t operation = 0, fail_at = 0, max_transfer = 256;
    std::string fail_operation, fail_path, corrupt_path;
    bool corrupt_once = false, zero_transfer = false, wrong_length = false;
    FileHandle next = 1;
    bool event(const char* op, const char* path) {
        ++operation;
        calls.push_back(std::string(op) + ":" + (path ? path : ""));
        snapshots.push_back(files);
        return (fail_at && operation == fail_at) ||
               (fail_operation == op && (fail_path.empty() || fail_path == path));
    }
    void save() { snapshots.push_back(files); }
    static StorageResult stat(void* context, const char* path, std::size_t& size) {
        Memory& m = *static_cast<Memory*>(context); size = 0;
        if (m.event("stat", path)) return StorageResult::Error;
        const auto it = m.files.find(path);
        if (it == m.files.end()) return StorageResult::Missing;
        size = it->second.size();
        if (m.wrong_length && std::string(path).find(".tmp") != std::string::npos) ++size;
        return StorageResult::Ok;
    }
    static StorageResult open(void* context, const char* path, FileHandle& handle, bool write) {
        Memory& m = *static_cast<Memory*>(context); handle = invalid_file_handle;
        if (m.event(write ? "open_write" : "open_read", path)) return StorageResult::Error;
        if (write) m.files[path].clear();
        else if (!m.files.count(path)) return StorageResult::Missing;
        handle = m.next++;
        m.opened.emplace(handle, Open{path, 0, write}); m.save();
        return StorageResult::Ok;
    }
    static StorageResult open_read(void* c, const char* p, FileHandle& h) { return open(c,p,h,false); }
    static StorageResult open_write(void* c, const char* p, FileHandle& h) { return open(c,p,h,true); }
    static bool seek(void* context, FileHandle handle, std::size_t offset) {
        Memory& m = *static_cast<Memory*>(context); Open& f = m.opened.at(handle);
        if (m.event("seek", f.name.c_str())) return false;
        if (offset > m.files.at(f.name).size()) return false;
        f.offset = offset; return true;
    }
    static StorageResult read(void* context, FileHandle handle, std::uint8_t* data,
                              std::size_t capacity, std::size_t& count) {
        Memory& m = *static_cast<Memory*>(context); Open& f = m.opened.at(handle); count = 0;
        if (m.event("read", f.name.c_str())) return StorageResult::Error;
        Bytes& bytes = m.files.at(f.name);
        if (!m.zero_transfer) count = std::min(std::min(capacity, m.max_transfer), bytes.size()-f.offset);
        if (count) std::memcpy(data, bytes.data()+f.offset, count);
        f.offset += count;
        if (count && !m.corrupt_path.empty() && f.name == m.corrupt_path) {
            data[0] ^= 0x80;
            if (m.corrupt_once) m.corrupt_path.clear();
        }
        return StorageResult::Ok;
    }
    static StorageResult write(void* context, FileHandle handle, const std::uint8_t* data,
                               std::size_t length, std::size_t& count) {
        Memory& m = *static_cast<Memory*>(context); Open& f = m.opened.at(handle); count = 0;
        if (m.event("write", f.name.c_str())) return StorageResult::Error;
        if (!m.zero_transfer) count = std::min(length, m.max_transfer);
        Bytes& bytes = m.files.at(f.name);
        bytes.insert(bytes.end(), data, data + count); f.offset += count; m.save();
        return StorageResult::Ok;
    }
    static bool sync(void* context, FileHandle handle) {
        Memory& m = *static_cast<Memory*>(context);
        return !m.event("sync", m.opened.at(handle).name.c_str());
    }
    static bool close(void* context, FileHandle handle) {
        Memory& m = *static_cast<Memory*>(context);
        const bool ok = !m.event("close", m.opened.at(handle).name.c_str());
        m.opened.erase(handle); return ok;
    }
    static StorageResult rename(void* context, const char* from, const char* to) {
        Memory& m = *static_cast<Memory*>(context);
        if (m.event("rename", from)) return StorageResult::Error;
        if (!m.files.count(from)) return StorageResult::Missing;
        if (m.files.count(to)) return StorageResult::Error;
        m.files[to] = m.files.at(from); m.files.erase(from); m.save();
        return StorageResult::Ok;
    }
    static StorageResult remove(void* context, const char* path) {
        Memory& m = *static_cast<Memory*>(context);
        if (m.event("remove", path)) return StorageResult::Error;
        const bool found = m.files.erase(path); m.save();
        return found ? StorageResult::Ok : StorageResult::Missing;
    }
    FileIo io() {
        FileIo result; result.context=this; result.stat=stat; result.open_read=open_read;
        result.open_write=open_write; result.seek=seek; result.read=read; result.write=write;
        result.sync=sync; result.close=close; result.rename=rename; result.remove=remove;
        return result;
    }
};
Bytes payload(std::size_t size, unsigned seed) {
    Bytes result(size); for (std::size_t n=0;n<size;++n) result[n]=static_cast<std::uint8_t>(n*37+seed);
    return result;
}
Bytes read_all(VerifiedStorage& storage, const char* key, StorageResult& result) {
    std::size_t copied=99,total=99;
    result=storage.read(key,0,nullptr,0,copied,total);
    require(copied==0,"size query copied bytes");
    if(result!=StorageResult::Ok) return Bytes();
    Bytes bytes(total);
    result=storage.read(key,0,bytes.empty()?nullptr:bytes.data(),bytes.size(),copied,total);
    if(result==StorageResult::Ok) require(copied==bytes.size(),"read length");
    return bytes;
}
void normal_and_recovery() {
    for (std::size_t size : {0u,1u,255u,256u,257u,1025u,8193u}) {
        Memory m; VerifiedStorage s(m.io()); const Bytes bytes=payload(size,9);
        require(s.valid(),"valid interface");
        require(s.write_atomic("cfg.a",bytes.data(),bytes.size())==StorageResult::Ok,"initial write");
        require(m.opened.empty(),"write handles closed");
        StorageResult result; require(read_all(s,"cfg.a",result)==bytes&&result==StorageResult::Ok,"initial readback");
        const Bytes update=payload(size+17,11); m.max_transfer=13;
        require(s.write_atomic("cfg.a",update.data(),update.size())==StorageResult::Ok,"partial transfers");
        require(read_all(s,"cfg.a",result)==update&&result==StorageResult::Ok,"partial readback");
        require(m.files.size()==1,"temporary and backup cleaned");
        std::size_t copied=0,total=0; std::uint8_t slice[9]={};
        require(s.read("cfg.a",update.size()-3,slice,sizeof(slice),copied,total)==StorageResult::Ok&&copied==3&&total==update.size(),"offset truncated read");
        require(!std::memcmp(slice,update.data()+update.size()-3,3),"offset bytes");
        require(s.read("cfg.a",update.size()+10,slice,sizeof(slice),copied,total)==StorageResult::Ok&&copied==0,"past end");
        require(s.remove("cfg.a")==StorageResult::Ok&&m.files.empty(),"remove existing");
        require(s.remove("cfg.a")==StorageResult::Missing,"remove missing");
    }
    Memory m; const Bytes old=payload(811,7),fresh=payload(903,8);
    m.files["/pb/cfg.a.bak"]=old; m.files["/pb/cfg.a.tmp"]=payload(18,2);
    VerifiedStorage s(m.io()); StorageResult result;
    require(read_all(s,"cfg.a",result)==old&&result==StorageResult::Ok,"backup read after interrupted rename");
    require(s.write_atomic("cfg.a",fresh.data(),fresh.size())==StorageResult::Ok,"recover then replace");
    require(m.files.size()==1&&m.files.at("/pb/cfg.a")==fresh,"recovery removes stale files");
    m.files["/pb/cfg.a.bak"]=old;
    m.fail_operation="stat";m.fail_path="/pb/cfg.a";
    require(read_all(s,"cfg.a",result).empty()&&result==StorageResult::Error,"stat error is not missing");
    m.fail_operation="read";
    (void)read_all(s,"cfg.a",result);
    require(result==StorageResult::Error,"main read error never falls back");
    m.fail_operation.clear();m.files["/pb/cfg.a"]=Bytes{0xff};
    require(read_all(s,"cfg.a",result)==Bytes{0xff}&&result==StorageResult::Ok,"main corruption is left to engine CRC");
}
void validation() {
    Memory m; VerifiedStorage s(m.io()); const std::uint8_t x=1;
    const char* invalid[]={nullptr,"","/cfg.a","../cfg.a","cfg..a",".a","cfg.","cfg/a","cfg\\a","cfg.a.tmp","cfg.a.bak","abcdefghijklmnopqrstuvwx","a\x80"};
    for(const char* key:invalid) {
        std::size_t got=5,total=8;
        require(s.read(key,0,nullptr,0,got,total)==StorageResult::Error&&got==0&&total==0,"invalid read key");
        require(s.write_atomic(key,&x,1)==StorageResult::Error,"invalid write key");
        require(s.remove(key)==StorageResult::Error,"invalid remove key");
    }
    require(m.operation==0,"invalid names cause no filesystem calls");
    require(s.write_atomic("ferm-cal",&x,1)==StorageResult::Ok,"hyphenated appliance record key");
    StorageResult result;require(read_all(s,"ferm-cal",result)==Bytes{1}&&result==StorageResult::Ok,"hyphenated appliance record readback");
    require(s.remove("ferm-cal")==StorageResult::Ok,"hyphenated appliance record removal");
    require(s.write_atomic("z-recovery.a",&x,1)==StorageResult::Ok,"hyphenated Z recovery bank");
    require(read_all(s,"z-recovery.a",result)==Bytes{1}&&result==StorageResult::Ok,"hyphenated Z recovery readback");
    require(s.remove("z-recovery.a")==StorageResult::Ok,"hyphenated Z recovery removal");
    require(s.write_atomic("abcdefghijklmnopqrstuvw",&x,1)==StorageResult::Ok,"23 byte key");
    for(const auto& call:m.calls) require(call.substr(call.find(':')+1).size()<32,"SPIFFS name bounded");
    const char* invalid_prefixes[]={"","pb/","/pb","/long/","/../",nullptr};
    for(const char* prefix:invalid_prefixes) {
        VerifiedStorage bad(m.io(),prefix);require(!bad.valid(),"invalid prefix");
    }
    FileIo missing=m.io();missing.sync=nullptr;VerifiedStorage bad(missing);
    require(!bad.valid()&&bad.write_atomic("cfg.a",&x,1)==StorageResult::Error,"missing callback");
    std::size_t got=0,total=0;
    require(s.read("cfg.a",0,nullptr,1,got,total)==StorageResult::Error,"null read buffer");
    require(s.write_atomic("cfg.a",nullptr,1)==StorageResult::Error,"null write buffer");
}
void faults_and_crashes() {
    const Bytes old=payload(641,3),fresh=payload(973,9),older=payload(43,17);
    Memory baseline;baseline.files["/pb/cfg.a"]=old;baseline.files["/pb/cfg.a.bak"]=older;baseline.files["/pb/cfg.a.tmp"]=Bytes{9};
    const Files initial=baseline.files;VerifiedStorage reference(baseline.io());
    require(reference.write_atomic("cfg.a",fresh.data(),fresh.size())==StorageResult::Ok,"baseline replacement");
    const std::size_t count=baseline.operation;
    for(std::size_t fail=1;fail<=count;++fail) {
        Memory m;m.files=initial;m.fail_at=fail;VerifiedStorage s(m.io());
        require(s.write_atomic("cfg.a",fresh.data(),fresh.size())==StorageResult::Error,"every callback failure returned");
        require(m.opened.empty(),"failure closes all handles");
        m.fail_at=0;StorageResult result;const Bytes bytes=read_all(s,"cfg.a",result);
        require(result==StorageResult::Ok&&(bytes==old||bytes==fresh),"failure preserves old or verified new bytes");
        ++failure_cases;
    }
    for(const Files& snapshot:baseline.snapshots) {
        Memory m;m.files=snapshot;VerifiedStorage s(m.io());StorageResult result;
        const Bytes bytes=read_all(s,"cfg.a",result);
        require(result==StorageResult::Ok&&(bytes==old||bytes==fresh),"remount after every step recovers complete generation");
        require(s.write_atomic("cfg.a",fresh.data(),fresh.size())==StorageResult::Ok,"next write after interrupted transaction");
        require(m.files.size()==1&&m.files.at("/pb/cfg.a")==fresh,"next write cleanup");
        ++crash_states;
    }
    for(const char* corrupt:{"/pb/cfg.a.tmp","/pb/cfg.a"}) {
        Memory m;m.files["/pb/cfg.a"]=old;m.corrupt_path=corrupt;m.corrupt_once=true;VerifiedStorage s(m.io());
        require(s.write_atomic("cfg.a",fresh.data(),fresh.size())==StorageResult::Error,"readback corruption rejected");
        StorageResult result;require(read_all(s,"cfg.a",result)==old&&result==StorageResult::Ok,"corruption restores previous generation");
    }
    for(bool wrong_size:{false,true}) {
        Memory m;m.files["/pb/cfg.a"]=old;m.wrong_length=wrong_size;m.zero_transfer=!wrong_size;VerifiedStorage s(m.io());
        require(s.write_atomic("cfg.a",fresh.data(),fresh.size())==StorageResult::Error,"zero progress and wrong size rejected");
        require(m.files.at("/pb/cfg.a")==old&&m.opened.empty(),"validation failure preserves old generation");
        require(!m.files.count("/pb/cfg.a.tmp"),"failed temporary write reclaims scratch space");
    }
    Memory deletion;deletion.files=initial;VerifiedStorage d(deletion.io());require(d.remove("cfg.a")==StorageResult::Ok,"delete baseline");
    for(const Files& snapshot:deletion.snapshots) {
        Memory m;m.files=snapshot;VerifiedStorage s(m.io());StorageResult result;const Bytes bytes=read_all(s,"cfg.a",result);
        require(result==StorageResult::Missing||(result==StorageResult::Ok&&bytes==old),"interrupted delete never resurrects older backup");
        ++crash_states;
    }
    const std::size_t delete_calls=deletion.operation;
    for(std::size_t fail=1;fail<=delete_calls;++fail) {
        Memory m;m.files=initial;m.fail_at=fail;VerifiedStorage s(m.io());
        require(s.remove("cfg.a")==StorageResult::Error,"deletion failure returned");
        m.fail_at=0;StorageResult result;const Bytes bytes=read_all(s,"cfg.a",result);
        require(result==StorageResult::Ok&&bytes==old,"failed deletion retains current data");
        ++failure_cases;
    }
    Memory first;VerifiedStorage first_storage(first.io());
    require(first_storage.write_atomic("cfg.a",fresh.data(),fresh.size())==StorageResult::Ok,"first-write baseline");
    for(std::size_t fail=1;fail<=first.operation;++fail) {
        Memory m;m.fail_at=fail;VerifiedStorage s(m.io());
        require(s.write_atomic("cfg.a",fresh.data(),fresh.size())==StorageResult::Error,"first-write failure returned");
        require(m.opened.empty(),"first-write failure closes handles");
        m.fail_at=0;StorageResult result;const Bytes bytes=read_all(s,"cfg.a",result);
        require(result==StorageResult::Missing||(result==StorageResult::Ok&&bytes==fresh),"failed first-write exposes no partial file");
        ++failure_cases;
    }
}
}
int main() {
    normal_and_recovery();validation();faults_and_crashes();
    std::printf("{\"checks\":%u,\"injected_callback_failures\":%u,\"interrupted_transaction_states\":%u,\"passed\":true}\n",checks,failure_cases,crash_states);
}
