#pragma once
#include "picobrew/device_services.hpp"
#include "json.hpp"
namespace picobrew { namespace standalone { namespace detail {
enum class RecordResult {Ok,Missing,Corrupt,Io};
class Records {
public:
    explicit Records(Storage storage):storage_(storage){}
    // A null/zero destination returns verified payload length/generation only.
    RecordResult load(const char* name,std::uint8_t* data,std::size_t capacity,std::size_t& length,std::uint32_t& generation,std::uint32_t expected=0,bool strict=false) const;
    // Verify both complete banks and the selected generation before returning
    // an exact slice. Large fixed-format schemas can decode directly into
    // their final allocation without a second full payload buffer.
    RecordResult load_slice(const char* name,std::size_t offset,std::uint8_t* data,std::size_t length,std::size_t total,std::uint32_t generation) const;
    RecordResult save(const char* name,const std::uint8_t* data,std::size_t length,std::uint32_t previous,std::uint32_t& generation) const;
private:
    Storage storage_;
};
}}}
