// Original Runtime and PicoStill diagnostic callbacks, using production code.
#pragma once
#include "zseries/application_console.hpp"
namespace zseries { namespace oem {
struct SoftwareConsoleClock {
    void* context=nullptr;
    std::uint32_t (*millis)(void*)=nullptr;
};
bool runtime_console_self_test(const ConsoleServices&,const ApplicationConsoleHooks&,const SoftwareConsoleClock&);
bool still_sensors_console_self_test(const ConsoleServices&,const ApplicationConsoleHooks&);
bool still_state_console_self_test(const ConsoleServices&,const ApplicationConsoleHooks&);
bool still_parser_console_self_test(const ConsoleServices&,const ApplicationConsoleHooks&);
bool still_backend_console_self_test(const ConsoleServices&,const ApplicationConsoleHooks&);
bool still_console_self_test(const ConsoleServices&,const ApplicationConsoleHooks&);
// Only these owned addresses are accepted; unsupported callbacks fail.
bool software_console_test(std::uint32_t original_callback,CoreApplication&,
    const ConsoleServices&,const ApplicationConsoleHooks&);
}} // namespace zseries::oem
