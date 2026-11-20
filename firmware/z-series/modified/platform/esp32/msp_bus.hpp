#pragma once
#include "zseries/msp_protocol.h"
#include <Wire.h>
#include <cstdint>
#include <cstddef>

namespace zseries { namespace standalone { namespace esp32 {
// The OEM transport waits forever for a short reply. The standalone profile
// instead checks the SDK transaction result and imposes its 50 ms bus deadline.
class MspBus {
public:
    explicit MspBus(TwoWire& wire) noexcept : wire_(wire) {}
    z_msp_transport transport() noexcept;
    bool failed() const noexcept { return failed_; }
private:
    TwoWire& wire_;
    bool failed_ = false;
    void transmit(std::uint8_t, const std::uint8_t*, std::size_t);
    void receive(std::uint8_t, std::uint8_t*, std::size_t);
};
}}}
