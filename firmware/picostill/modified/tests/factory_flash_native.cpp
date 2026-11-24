#include "factory_flash.hpp"
#include <mbedtls/sha256.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace picobrew::esp32_ota;
namespace {
esp_partition_t parts[] = {
    {ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,0x10000,RecoveryBytes,"pb_recovery",false},
    {ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,SlotOffset,SlotBytes,"pb_app",false},
    {ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,0x9000,0x5000,"nvs",false},
    {ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,0xe000,0x2000,"otadata",false},
    {ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,FilesystemOffset,FilesystemBytes,"spiffs",false}
};
auto& factory=parts[0]; auto& app=parts[1];
const esp_partition_t *running=&app, *selected=&app;
std::vector<unsigned char> original(2075,0x2a), active, recovery;
bool corrupt_native=false, fail_selection=false, handle_open=false, short_read=false;
unsigned erase_count=0, switches=0, off=0;
const std::vector<unsigned char>& bank(const esp_partition_t* p) { return p==&factory?recovery:active; }
}
esp_partition_iterator_t esp_partition_find(esp_partition_type_t type,esp_partition_subtype_t,const char*) {
    return type==ESP_PARTITION_TYPE_APP ? &parts[0]:&parts[2];
}
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t i) { return i==&parts[1]||i==&parts[4]?nullptr:i+1; }
const esp_partition_t* esp_partition_find_first(esp_partition_type_t type,esp_partition_subtype_t sub,const char* name) {
    for(auto& p:parts) if(p.type==type && p.subtype==sub && !std::strcmp(name,p.label)) return &p;
    return nullptr;
}
const esp_partition_t* esp_ota_get_running_partition(){return running;}
const esp_partition_t* esp_ota_get_boot_partition(){return selected;}
esp_err_t esp_image_verify(int,const esp_partition_pos_t* pos,esp_image_metadata_t* meta) {
    assert(pos->size==(pos->offset==0x10000?RecoveryBytes:SlotBytes));
    if (corrupt_native || (pos->offset!=factory.address && pos->offset!=app.address)) return -1;
    meta->image.hash_appended=true; meta->image.segment_count=2;
    meta->segments[0].load_addr=0x3f400020; meta->segments[0].data_len=512;
    meta->image_len=original.size(); return 0;
}
esp_err_t esp_partition_read(const esp_partition_t* p,unsigned at,void* dest,size_t n) {
    auto& v=bank(p); if(short_read || at>v.size() || n>v.size()-at)return -1;
    std::memcpy(dest,v.data()+at,n);return 0;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* p) {
    if (fail_selection)return -1;
    ++switches; selected=p; return 0;
}
esp_err_t esp_ota_begin(const esp_partition_t* p,size_t n,esp_ota_handle_t* h) {
    assert(p==&app && selected==&factory && running==&factory && n<=SlotBytes && !handle_open);
    active.assign(n,0xff); off=0;handle_open=true;*h=78;++erase_count;return 0;
}
esp_err_t esp_ota_write(esp_ota_handle_t h,const void* src,size_t n) {
    assert(h==78 && handle_open && off<=active.size() && n<=active.size()-off);
    std::memcpy(active.data()+off,src,n);off+=n;return 0;
}
esp_err_t esp_ota_end(esp_ota_handle_t h) { assert(h==78 && handle_open);handle_open=false;return corrupt_native?-1:0; }
unsigned esp_random(){return 47;}
// A deterministic digest port records every flash read. The target links mbedTLS;
// real SHA-256 and the actual ESP native footer are also checked by packaging.
void mbedtls_sha256_init(mbedtls_sha256_context* c) {std::memset(c,0,sizeof(*c));}
int mbedtls_sha256_starts_ret(mbedtls_sha256_context*,int){return 0;}
int mbedtls_sha256_update_ret(mbedtls_sha256_context* c,const unsigned char* p,size_t n) {
    for(size_t i=0;i<n;++i) c->digest[(c->consumed++)%32]^=p[i];
    return 0;
}
int mbedtls_sha256_finish_ret(mbedtls_sha256_context* c,unsigned char* output) {
    assert(c->consumed==original.size());std::memcpy(output,c->digest,32);return 0;
}
void mbedtls_sha256_free(mbedtls_sha256_context*) {}
int main() {
    std::memset(original.data()+0x120,0,64);std::strcpy(reinterpret_cast<char*>(original.data()+0x120),PB_FIELD_IDENTITY);
    const unsigned native_magic=ESP_APP_DESC_MAGIC_WORD;
    std::memcpy(original.data()+32,&native_magic,sizeof(native_magic));
    active=recovery=original;
    std::strcpy(reinterpret_cast<char*>(recovery.data()+0x120),PB_RECOVERY_IDENTITY);
    const auto immutable=recovery;
    if (std::strcmp(IdentityPrefix,RecoveryPrefix)) {
        recovery=active;FactoryFlash field_in_factory;field_in_factory.inspect();
        assert(field_in_factory.role()==Role::Unavailable);recovery=immutable;
    }
    FactoryFlash normal; normal.inspect();assert(normal.role()==Role::Application && normal.running_size()==original.size());
    std::string sha=normal.running_sha256();assert(sha.size()==64 && !normal.start(12));
    corrupt_native=true;assert(!normal.select_recovery() && selected==&app);corrupt_native=false;
    assert(normal.select_recovery() && selected==&factory);
    running=selected;FactoryFlash rescue;rescue.inspect();assert(rescue.role()==Role::Recovery);
    // If the native bootloader fell back to factory, it must make that selection durable before the erase.
    selected=&app;fail_selection=true;assert(!rescue.start(32) && erase_count==0);fail_selection=false;
    assert(rescue.start(original.size()) && selected==&factory);
    assert(rescue.write(original.data(),100));rescue.abandon();assert(selected==&factory && !handle_open);
    assert(rescue.start(original.size()) && rescue.write(original.data(),original.size()));
    assert(rescue.matches(5,original.data()+5,700) && !rescue.matches(0,original.data()+0x120,20));
    active.back()^=1;assert(!rescue.finish(original.size(),Version,sha.c_str()) && selected==&factory);
    auto upload=[&](){assert(rescue.start(original.size()) && rescue.write(original.data(),original.size()));};
    upload();assert(!rescue.finish(original.size()-1,Version,sha.c_str()));
    upload();assert(!rescue.finish(original.size(),"different",sha.c_str()));
    upload();corrupt_native=true;assert(!rescue.finish(original.size(),Version,sha.c_str()));corrupt_native=false;
    upload();short_read=true;assert(!rescue.finish(original.size(),Version,sha.c_str()));short_read=false;
    upload();fail_selection=true;assert(!rescue.finish(original.size(),Version,sha.c_str()));fail_selection=false;
    upload();assert(rescue.finish(original.size(),Version,sha.c_str()) && selected==&app && recovery==immutable);
    app.size--;FactoryFlash broken;broken.inspect();assert(broken.role()==Role::InvalidRecovery && !broken.start(100));app.size++;
    running=&app;recovery[0x120]^=8;FactoryFlash missing;missing.inspect();assert(missing.role()==Role::Unavailable && !missing.select_recovery());
    puts("Factory flash gates, native image rejection, readback coverage, durable fallback and exact selection passed");
}
