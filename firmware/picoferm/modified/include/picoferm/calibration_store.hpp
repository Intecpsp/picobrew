#pragma once
#include "picobrew/device_services.hpp"
#include <cstddef>
#include <cstdint>

namespace picoferm { namespace standalone {
struct Configuration {
    std::uint32_t offset = 0, sampling_seconds = 300;
    std::int32_t slope = 110840;
    bool calibrated = false;
};
class CalibrationStore {
public:
    explicit CalibrationStore(picobrew::standalone::Storage storage) noexcept : storage_(storage) {}
    // OEM bytes are borrowed for read-only import, and are never passed to a
    // write callback. A malformed existing modified record is not replaced.
    picobrew::standalone::Result load(const std::uint8_t* oem, std::size_t size);
    picobrew::standalone::Result save(const Configuration& value);
    const Configuration& value() const noexcept { return value_; }
    bool healthy() const noexcept { return healthy_; }
    static bool valid_offset(std::uint32_t value) noexcept { return value >= 34470 && value <= 234000; }
    static bool valid(const Configuration&) noexcept;
private:
    picobrew::standalone::Storage storage_;
    Configuration value_;
    bool healthy_ = false;
    picobrew::standalone::Result write(const Configuration&);
};
}}
