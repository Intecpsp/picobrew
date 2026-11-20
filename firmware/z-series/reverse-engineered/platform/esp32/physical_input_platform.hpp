#pragma once
#include "zseries/physical_inputs.hpp"

namespace zseries { namespace oem { namespace esp32 {
struct PhysicalInputBindings {
    PhysicalEncoder* encoder = nullptr;
    volatile PhysicalInputResult last_interrupt_result = PhysicalInputResult::Ok;
};
// The original firmware has one physical encoder. The binding and encoder
// must outlive attached interrupts; application assembly owns those objects.
// Console/event services remain required application bindings on the encoder.
PhysicalInputIo physical_input_io(PhysicalInputBindings&) noexcept;
}}} // namespace zseries::oem::esp32
