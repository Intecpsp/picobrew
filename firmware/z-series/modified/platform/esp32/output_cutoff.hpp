#pragma once
namespace zseries { namespace standalone { namespace esp32 {
// The MSP has an independent power/reset domain; reset the pump mask over I2C.
bool force_outputs_off();
}}}
