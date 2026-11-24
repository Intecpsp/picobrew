#include "transfer.hpp"
#include "bootstrap_identity.h"
#include "json.hpp"
extern "C" {
#include "sha256.h"
}
#include <cinttypes>
#include <cstdio>
#include <cstring>
#ifdef ESP8266
#include <pgmspace.h>
#define OTA_PRINTF(dst,capacity,format,...) ::snprintf_P(dst,capacity,PSTR(format),##__VA_ARGS__)
#else
#define OTA_PRINTF(dst,capacity,format,...) ::snprintf(dst,capacity,format,##__VA_ARGS__)
#endif

namespace picoferm {
using picobrew::standalone::detail::Json;
namespace {
enum Mask { UID=1, Model=2, Version=4, Hash=8, Size=16, Session=32, Offset=64, Data=128 };
struct Request {
    char uid[40]{}, model[17]{}, version[19]{}, hash[65]{}, session[17]{};
    uint32_t size=0, offset=0, length=0;
    const char* data=nullptr;
    unsigned fields=0;
    bool parse(const PicoHttpRequest& request) {
        Json json(request.body,request.body_length);
        if (!json.token('{')) return false;
        while (!json.peek('}')) {
            char key[40]; int64_t number;
            if (!json.key(key)) return false;
            unsigned field;
            if (!std::strcmp(key,"device_uid")) { field=UID; if (!json.string(uid,sizeof(uid))) return false; }
            else if (!std::strcmp(key,"model")) { field=Model; if (!json.string(model,sizeof(model))) return false; }
            else if (!std::strcmp(key,"version")) { field=Version; if (!json.string(version,sizeof(version))) return false; }
            else if (!std::strcmp(key,"sha256")) { field=Hash; if (!json.string(hash,sizeof(hash))) return false; }
            else if (!std::strcmp(key,"session")) { field=Session; if (!json.string(session,sizeof(session))) return false; }
            else if (!std::strcmp(key,"size") || !std::strcmp(key,"offset")) {
                field = key[0]=='s' ? Size : Offset;
                if (!json.integer(0, FERM_BANK_SIZE, number)) return false;
                if (field==Size) size=static_cast<uint32_t>(number);
                else offset=static_cast<uint32_t>(number);
            } else if (!std::strcmp(key,"data")) {
                field=Data;
                if (!json.token('"')) return false;
                data=json.p;
                while (json.p < json.end && *json.p != '"') {
                    if (length == 1024 || json.end - json.p < 2) return false;
                    int a=Json::hex(*json.p++), b=Json::hex(*json.p++);
                    if (a<0 || b<0) return false;
                    ++length;
                }
                if (!json.token('"')) return false;
            } else return false;
            if (fields & field) return false;
            fields |= field;
            if (!json.next('}')) break;
        }
        return json.token('}') && json.done();
    }
};
bool version_ok(const char* version) {
    unsigned components=1, digits=0;
    for (const char *p=version; *p; ++p) {
        if (*p=='.' && digits && components<3) { ++components; digits=0; }
        else if (*p>='0' && *p<='9' && ++digits<=5) continue;
        else return false;
    }
    return components==3 && digits;
}
bool hash_ok(const char* hash) {
    return std::strlen(hash)==64 && std::strspn(hash,"0123456789abcdef")==64;
}
void as_hex(const uint8_t bytes[32], char out[65]) {
    static const char digits[]="0123456789abcdef";
    for (unsigned i=0;i<32;++i) {
        out[2*i]=digits[bytes[i]>>4]; out[2*i+1]=digits[bytes[i]&15u];
    }
    out[64]=0;
}
bool hash_matches(const uint8_t bytes[32], const char* hex) {
    for (unsigned i=0;i<32;++i)
        if (bytes[i] != (Json::hex(hex[i*2])*16+Json::hex(hex[i*2+1]))) return false;
    return true;
}
}
bool Update::digest(uint32_t address, uint32_t size, uint8_t hash[32], uint32_t* out_crc) {
    uint32_t block[64], crc=0;
    PicoRecoverySha256 sha;
    pico_recovery_sha256_init(&sha);
    while (size) {
        uint32_t n = size > sizeof(block) ? sizeof(block) : size;
        if (flash_.io.read(flash_.io.context,address,block,n)) return false;
        pico_recovery_sha256_update(&sha,reinterpret_cast<const uint8_t*>(block),n);
        if (out_crc) crc=ferm_crc32(crc,block,n);
        address+=n; size-=n;
    }
    pico_recovery_sha256_final(&sha,hash);
    if (out_crc) *out_crc=crc;
    return true;
}
bool Update::attach(unsigned running, const char* uid, const char* version, bool storage_ready) {
    uint8_t sha[32];
    if (running>1 || !ferm_read_record(&flash_.io,running,&current_)) return false;
    // A trial that cannot pass setup must still time out and fall back.
    trial_=current_.state==FERM_TRIED;
    if (!uid || !*uid || std::strlen(uid)>=sizeof(uid_) ||
        std::strspn(uid,"0123456789abcdefABCDEF:-")!=std::strlen(uid) ||
        !version_ok(version) || std::strlen(version)>=sizeof(version_)) return false;
    if (!digest(0,FERM_LOADER_LENGTH,sha,nullptr) || std::memcmp(sha,ferm_loader_sha,32) ||
        !ferm_image(&flash_.io,&current_,0,nullptr) ||
        (current_.state!=FERM_CONFIRMED && current_.state!=FERM_TRIED)) return false;
    if (!digest(FERM_BANK_ADDRESS(running),current_.size,image_digest_,nullptr)) return false;
    running_=running; std::strcpy(uid_,uid); std::strcpy(version_,version);
    available_=storage_ready;
    return available_;
}
void Update::cancel() {
    if (reboot_ || trial_) return;
    active_=false; session_[0]=0; hash_[0]=0; target_[0]=0; written_=0; size_=0;
}
bool Update::begin(const char* version, const char* hash, uint32_t size) {
    if (!version_ok(version) || !std::strcmp(version_,version) || !hash_ok(hash) ||
        size<64 || size>FERM_BANK_SIZE || (size&15) || current_.generation==UINT32_MAX) return false;
    if (session_[0])
        return size==size_ && !std::strcmp(hash_,hash) && !std::strcmp(target_,version);
    if (!flash_.erase(flash_.io.context,FERM_RECORD_ADDRESS(1u-running_)/FERM_SECTOR)) return false;
    OTA_PRINTF(session_,sizeof(session_),"%08" PRIx32 "%08" PRIx32,
                  flash_.random(flash_.io.context),flash_.random(flash_.io.context));
    std::strcpy(target_,version); std::strcpy(hash_,hash); size_=size; written_=0;
    return true;
}
bool Update::chunk(uint32_t offset,const char* hex,uint32_t length) {
    if (!length || (length&3) || (offset&3) || offset>size_ || length>size_-offset || offset>written_) return false;
    uint32_t base=FERM_BANK_ADDRESS(1u-running_);
    bool duplicate=offset<written_;
    if (duplicate && length>written_-offset) return false;
    uint32_t verify[32], words[32];
    auto* bytes=reinterpret_cast<uint8_t*>(words);
    for (unsigned pos=0;pos<length;) {
        uint32_t addr=base+offset+pos;
        uint32_t n=FERM_SECTOR-addr%FERM_SECTOR;
        if (n>sizeof(verify)) n=sizeof(verify);
        if (n>length-pos) n=length-pos;
        for (unsigned i=0;i<n;++i) {
            bytes[i]=static_cast<uint8_t>(Json::hex(hex[2*(pos+i)])*16+Json::hex(hex[2*(pos+i)+1]));
        }
        if (!duplicate) {
            if (!(addr%FERM_SECTOR) && !flash_.erase(flash_.io.context,addr/FERM_SECTOR)) return false;
            if (flash_.io.write(flash_.io.context,addr,bytes,n)) return false;
        }
        if (flash_.io.read(flash_.io.context,addr,verify,n) || std::memcmp(verify,bytes,n)) return false;
        pos+=n;
    }
    if (!duplicate) written_+=length;
    return true;
}
bool Update::finish() {
    if (!size_ || written_!=size_) return false;
    unsigned bank=1u-running_;
    uint32_t base=FERM_BANK_ADDRESS(bank);
    alignas(4) char identity[32], expected[32]{};
    OTA_PRINTF(expected,sizeof(expected),"PicoFerm-AB1:%s",target_);
    uint8_t sha[32]; uint32_t crc;
    if (flash_.io.read(flash_.io.context,base+16,identity,sizeof(identity)) ||
        std::memcmp(identity,expected,sizeof(identity)) ||
        !digest(base,size_,sha,&crc) || !hash_matches(sha,hash_)) return false;
    FermBootRecord record={FERM_RECORD_MAGIC,bank,current_.generation+1,size_,crc,0,FERM_PENDING};
    record.header_crc=ferm_crc32(0,&record,offsetof(FermBootRecord,header_crc));
    if (!ferm_image(&flash_.io,&record,0,nullptr)) return false;
    const unsigned body=offsetof(FermBootRecord,state);
    uint32_t address=FERM_RECORD_ADDRESS(bank);
    FermBootRecord check;
    if (flash_.io.write(flash_.io.context,address,&record,body) ||
        flash_.io.read(flash_.io.context,address,&check,body) || std::memcmp(&record,&check,body) ||
        flash_.io.write(flash_.io.context,address+body,&record.state,4) ||
        !ferm_read_record(&flash_.io,bank,&check) || std::memcmp(&record,&check,sizeof(record))) return false;
    reboot_=true;
    return true;
}
bool Update::handle(const PicoHttpRequest& req, PicoHttpResponse& res, char* output, size_t cap, bool idle) {
    const char* prefix="/api/v1/firmware/ota";
    size_t n=std::strlen(prefix);
    if (std::strncmp(req.path,prefix,n) || (req.path[n] && req.path[n]!='/')) return false;
    const char* path=req.path+n;
    unsigned status=200;
    bool info=false;
    Request input;
    if (!req.auth_configured || !req.authenticated) status=401;
    else if (!available_) status=503;
    else if (!*path && req.method==PICO_HTTP_GET) info=true;
    else if (req.method!=PICO_HTTP_POST) status=405;
    else if (!input.parse(req)) status=400;
    else if (!std::strcmp(path,"/confirm")) {
        if (input.fields!=(UID|Version) || std::strcmp(input.uid,uid_) || std::strcmp(input.version,version_)) status=409;
        else if (trial_ && !ferm_transition(&flash_.io,running_,FERM_TRIED,FERM_CONFIRMED)) status=503;
        else { trial_=false; current_.state=FERM_CONFIRMED; }
    } else if (reboot_ || trial_ || !idle) status=409;
    else if (!std::strcmp(path,"/enter")) {
        if (input.fields!=UID || std::strcmp(input.uid,uid_)) status=409;
        else { active_=true; status=202; }
    } else if (!active_) status=409;
    else if (!std::strcmp(path,"/abort")) {
        if (input.fields!=UID || std::strcmp(input.uid,uid_)) status=409;
        else cancel();
    } else if (!std::strcmp(path,"/begin")) {
        if (input.fields!=(UID|Model|Version|Hash|Size) || std::strcmp(input.uid,uid_) ||
            std::strcmp(input.model,"picoferm") || !begin(input.version,input.hash,input.size)) status=409;
    } else if (!session_[0] || std::strcmp(input.session,session_)) status=409;
    else if (!std::strcmp(path,"/chunk")) {
        if (input.fields!=(Session|Offset|Data) || !chunk(input.offset,input.data,input.length)) status=409;
    } else if (!std::strcmp(path,"/finish")) {
        if (input.fields!=Session || !finish()) status=409; else status=202;
    } else status=404;
    int written;
    if (status>=400) written=OTA_PRINTF(output,cap,"{\"error\":\"firmware_request_rejected\"}");
    else if (info) {
        char image_sha[65], loader_sha[65];
        as_hex(image_digest_,image_sha);
        as_hex(ferm_loader_sha,loader_sha);
        written=OTA_PRINTF(output,cap,
        "{\"protocol\":\"esp8266-ab-v1\",\"model\":\"picoferm\",\"device_uid\":\"%s\","
        "\"running_bank\":%u,\"image_size\":%" PRIu32 ",\"image_sha256\":\"%s\",\"bootstrap_sha256\":\"%s\","
        "\"mode\":\"%s\",\"version\":\"%s\",\"target_bank\":%u,\"trial\":%s,\"max_bytes\":%u,"
        "\"chunk_bytes\":1024,\"session\":\"%s\",\"offset\":%" PRIu32 ",\"size\":%" PRIu32 ","
        "\"sha256\":\"%s\",\"target_version\":\"%s\"}",uid_,running_,current_.size,image_sha,loader_sha,active_?"recovery":"application",version_,
        1u-running_,trial_?"true":"false",FERM_BANK_SIZE,session_,written_,size_,hash_,target_);
    } else written=OTA_PRINTF(output,cap,"{\"session\":\"%s\",\"offset\":%" PRIu32 "}",session_,written_);
    if (written<0 || static_cast<size_t>(written)>=cap) { status=503; written=0; }
    res.status=status; res.content_type="application/json"; res.content_encoding=nullptr;
    res.body=reinterpret_cast<const uint8_t*>(output); res.body_length=static_cast<size_t>(written);
    return true;
}
}
