#pragma once
#include "picobrew/verified_storage.hpp"

namespace picobrew { namespace standalone { namespace esp32 {
class SpiffsFiles {
public:
    // Never formats an unrecognized or corrupt partition.
    bool mount();
    bool mounted() const noexcept { return mounted_; }
    bool capacity(std::size_t& total,std::size_t& used) const;
    picobrew::standalone::FileIo io() noexcept;
private:
    struct OpenFile {int descriptor=-1;bool used=false,writing=false;char path[48]={};};
    OpenFile open_[6];
    bool mounted_=false;
    bool path(const char*,char(&out)[48]) const noexcept;
    OpenFile* file(picobrew::standalone::FileHandle) noexcept;
    picobrew::standalone::StorageResult open(const char*,bool,picobrew::standalone::FileHandle&);
};
}}}
