#pragma once
#include "picobrew/verified_storage.hpp"

namespace picobrew { namespace standalone { namespace esp8266 {

struct SpiffsGeometry {
    std::uint32_t start = 0;
    std::uint32_t size = 0;
    std::uint32_t page_size = 0;
    std::uint32_t block_size = 0;
};

// Owns the partition, its mount buffers and the shared verified transaction.
// Do not mount the same partition through Arduino's global SPIFFS concurrently.
// begin NEVER formats: a blank, damaged or unreadable partition fails explicitly.
class SpiffsStorage {
public:
    explicit SpiffsStorage(const char* prefix = "/pb/") noexcept;
    explicit SpiffsStorage(SpiffsGeometry, const char* prefix = "/pb/") noexcept;
    ~SpiffsStorage();
    SpiffsStorage(const SpiffsStorage&) = delete;
    SpiffsStorage& operator=(const SpiffsStorage&) = delete;
    bool begin();
    void end() noexcept;
    bool mounted() const noexcept;
    std::int32_t last_error() const noexcept;
    Storage binding() noexcept;
private:
    struct Impl;
    Impl* impl_;
    VerifiedStorage storage_;
};

}}}
