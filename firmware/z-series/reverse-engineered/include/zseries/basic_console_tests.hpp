// Original Basic diagnostic leaves and Core::basicTests (Z119).
#pragma once
#include "zseries/application_console.hpp"

namespace zseries { namespace oem {
// Executes only recognized recovered leaf addresses. Unknown callbacks fail;
// no successful placeholder substitutes for an unimplemented diagnostic.
bool basic_console_leaf(std::uint32_t original_callback, const ConsoleServices&,
                        const ApplicationConsoleHooks&);
// The suite's only instance-specific operation is the Head test dispatch,
// supplied by hooks.invoke. This overload also permits a complete native replay
// without constructing unrelated controller peripherals.
bool basic_console_self_test(const ConsoleServices&, const ApplicationConsoleHooks&);
bool basic_console_self_test(CoreApplication&, const ConsoleServices&,
                             const ApplicationConsoleHooks&);
bool basic_console_test(std::uint32_t original_callback, CoreApplication&,
                        const ConsoleServices&, const ApplicationConsoleHooks&);
}} // namespace zseries::oem
