#include "output_cutoff.hpp"
#include "msp_bus.hpp"
#include "zseries/heater.hpp"
namespace zseries { namespace standalone { namespace esp32 {
bool force_outputs_off() {
    pinMode(oem::OriginalHeaterEnablePin, OUTPUT);
    digitalWrite(oem::OriginalHeaterEnablePin, LOW);
    pinMode(oem::OriginalHeaterOutputPin, OUTPUT);
    digitalWrite(oem::OriginalHeaterOutputPin, LOW);
    Wire1.begin(25, 26, 400000);
    MspBus cutoff(Wire1);
    auto io = cutoff.transport();
    std::uint8_t frame[6];
    const auto size = z_msp_app_frame(frame, sizeof(frame), Z_MSP_PUMPS_SET, 0, 1);
    io.transmit(io.context, 0x40, frame, size);
    return !cutoff.failed();
}
}}}
