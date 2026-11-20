// Recovered concrete hardware/data diagnostic test callbacks.
#pragma once
#include "zseries/console.hpp"
#include <cstdint>
namespace zseries { namespace oem {
class CoreApplication;
struct ApplicationConsoleHooks;
bool eeprom_console_self_test(const ConsoleServices&,const ApplicationConsoleHooks&);
bool max11613_console_self_test(const ConsoleServices&,const ApplicationConsoleHooks&);
// Only recognized owned callbacks are accepted. No external dependency is
// represented by a successful empty callback.
bool hardware_console_test(std::uint32_t original_callback,CoreApplication&,
    const ConsoleServices&,const ApplicationConsoleHooks&);
}} // namespace zseries::oem
