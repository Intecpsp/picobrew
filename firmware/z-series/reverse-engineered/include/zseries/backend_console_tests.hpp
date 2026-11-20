// Original backend data tests and concrete live diagnostic suites.
#pragma once
#include "zseries/application_console.hpp"
namespace zseries { namespace oem {
bool http_backend_console_self_test(const ConsoleServices&, const ApplicationConsoleHooks&);
bool z_backend_console_self_test(const ConsoleServices&, const ApplicationConsoleHooks&);
bool backend_console_test(std::uint32_t original_callback, CoreApplication&,
    const ConsoleServices&, const ApplicationConsoleHooks&);
}} // namespace zseries::oem
