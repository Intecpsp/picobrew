#pragma once
#include "zseries/msp_protocol.h"
#include "zseries/console.hpp"
#include <cstdint>
namespace zseries { namespace oem {
struct ApplicationConsoleHooks;
// The original tests perform real MSP reads, switching, erasure and programming.
// They require the live transport and returning diagnostic services; they do
// not substitute simulated device success in the firmware application.
bool msp_console_self_test(std::uint32_t original_callback,z_msp_client&,
    const z_msp_image& production,const z_msp_image& unit_test,
    const ConsoleServices&,const ApplicationConsoleHooks&);
}} // namespace zseries::oem
