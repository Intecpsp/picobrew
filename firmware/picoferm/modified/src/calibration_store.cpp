#include "picoferm/calibration_store.hpp"
#include "picoferm/core.hpp"
#include <cstring>

namespace picoferm { namespace standalone {
namespace {
namespace api = picobrew::standalone;
const char* const key = "ferm-cal";
constexpr std::size_t record_size = 24;
std::uint32_t get(const std::uint8_t* p) { return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }
void put(std::uint8_t* p, std::uint32_t value) { for (unsigned i=0; i<4; ++i) p[i]=static_cast<std::uint8_t>(value >> (i*8)); }
std::uint32_t crc(const std::uint8_t* p, std::size_t size) {
    std::uint32_t value = 0xffffffffu;
    for (std::size_t i=0; i<size; ++i) { value ^= p[i]; for (unsigned bit=0; bit<8; ++bit) value=(value>>1) ^ ((value&1)?0xedb88320u:0); }
    return ~value;
}
api::Result storage_error() { return {503,"calibration_storage","The modified calibration/settings record is not verified"}; }
void encode(const Configuration& value, std::uint8_t (&bytes)[record_size]) {
    std::memset(bytes,0,sizeof(bytes)); std::memcpy(bytes,"PFC1",4);
    bytes[4]=1; bytes[5]=value.calibrated?1:0;
    put(bytes+8,value.offset); put(bytes+12,static_cast<std::uint32_t>(value.slope));
    put(bytes+16,value.sampling_seconds); put(bytes+20,crc(bytes,20));
}
bool decode(const std::uint8_t (&bytes)[record_size], Configuration& value) {
    if (std::memcmp(bytes,"PFC1",4) || bytes[4]!=1 || bytes[5]>1 || bytes[6] || bytes[7] || get(bytes+20)!=crc(bytes,20)) return false;
    value.offset=get(bytes+8); const auto slope=get(bytes+12); std::memcpy(&value.slope,&slope,4);
    value.sampling_seconds=get(bytes+16); value.calibrated=bytes[5]!=0;
    return CalibrationStore::valid(value);
}
}
bool CalibrationStore::valid(const Configuration& value) noexcept {
    return value.slope>=90000 && value.slope<=150000 && value.sampling_seconds>=60 && value.sampling_seconds<=86400 &&
        (value.calibrated ? valid_offset(value.offset) : value.offset==0);
}
api::Result CalibrationStore::load(const std::uint8_t* original, std::size_t size) {
    healthy_=false;
    if (!storage_.read || !storage_.write_atomic) return storage_error();
    std::uint8_t bytes[record_size]={}; std::size_t count=0,total=0;
    const auto result=storage_.read(storage_.context,key,0,bytes,sizeof(bytes),count,total);
    if (result==api::StorageResult::Ok) {
        Configuration decoded;
        if (count!=sizeof(bytes) || total!=sizeof(bytes) || !decode(bytes,decoded)) return storage_error();
        value_=decoded; healthy_=true; return {};
    }
    if (result!=api::StorageResult::Missing) return storage_error();
    Configuration imported;
    if (original && size==oem::PersistentData::size && get(original+oem::PersistentData::cookie_position)==0xdeadbeefu &&
        get(original+oem::PersistentData::version_position)==1) {
        const auto slope=get(original+oem::PersistentData::slope_position);
        if (slope>=90000 && slope<=150000) {
            imported.slope=static_cast<std::int32_t>(slope);
            const auto offset=get(original+oem::PersistentData::pressure_offset_position);
            if (original[oem::PersistentData::has_offset_position] && valid_offset(offset)) { imported.offset=offset; imported.calibrated=true; }
        }
    }
    return write(imported);
}
api::Result CalibrationStore::write(const Configuration& next) {
    std::uint8_t bytes[record_size],checked[record_size]={}; encode(next,bytes);
    if (storage_.write_atomic(storage_.context,key,bytes,sizeof(bytes))!=api::StorageResult::Ok) { healthy_=false; return storage_error(); }
    std::size_t count=0,total=0;
    if (storage_.read(storage_.context,key,0,checked,sizeof(checked),count,total)!=api::StorageResult::Ok ||
        count!=sizeof(checked) || total!=sizeof(checked) || std::memcmp(bytes,checked,sizeof(bytes))) { healthy_=false; return storage_error(); }
    value_=next; healthy_=true; return {};
}
api::Result CalibrationStore::save(const Configuration& next) {
    if (!healthy_) return storage_error();
    if (!valid(next)) return {400,"invalid_calibration","Calibration or sampling interval is outside its supported range"};
    return write(next);
}
}}
