#include "physical_input_platform.hpp"
#include <Arduino.h>

namespace zseries { namespace oem { namespace esp32 {
namespace {
PhysicalInputBindings* interrupt_binding = nullptr;
std::uint32_t clock(void*) { return ::millis(); }
bool pin_mode(void*, std::uint8_t pin, std::uint8_t mode) { ::pinMode(pin, mode); return true; }
bool IRAM_ATTR read_pin(void*, std::uint8_t pin, std::int32_t& value) {
    value = ::digitalRead(pin); return true;
}
void IRAM_ATTR encoder_change() {
    interrupt_binding->last_interrupt_result = interrupt_binding->encoder->interrupt();
}
// Original 0x4008c210 is ENTRY/RETW only. The button is sampled by its tick.
void IRAM_ATTR button_falling() {}
bool attach(void* context, std::uint8_t pin, InputInterrupt interrupt, std::uint8_t mode) {
    auto& binding = *static_cast<PhysicalInputBindings*>(context);
    // The historical SDK indexes its 40-entry ISR table without a bounds
    // check. Reject the original invalid-pin sentinel before that undefined
    // memory access; all evidenced board pins are below 40.
    if (pin >= 40 || !binding.encoder || (interrupt_binding && interrupt_binding != &binding)) return false;
    interrupt_binding = &binding;
    ::attachInterrupt(pin, interrupt == InputInterrupt::EncoderChange ? encoder_change : button_falling, mode);
    return true;
}
}
PhysicalInputIo physical_input_io(PhysicalInputBindings& binding) noexcept {
    PhysicalInputIo result;
    result.context = &binding; result.millis = clock; result.pin_mode = pin_mode;
    result.read_pin = read_pin; result.attach_interrupt = attach;
    return result;
}
}}} // namespace zseries::oem::esp32
