// Shared recovered diagnostic suite marker (0x400ee670 / 0x4010a26c).
#pragma once
#include "zseries/application_console.hpp"
namespace zseries { namespace oem {
class ConsoleTestMarker {
public:
    ConsoleTestMarker(const ConsoleServices&, const ApplicationConsoleHooks&, const char* category) noexcept;
    bool begin();
    bool end();
    ~ConsoleTestMarker();
    ConsoleTestMarker(const ConsoleTestMarker&) = delete;
    ConsoleTestMarker& operator=(const ConsoleTestMarker&) = delete;
private:
    const ConsoleServices& services_;
    const ApplicationConsoleHooks& hooks_;
    const char* category_;
    std::uint32_t date_=0, assertions_=0;
    bool active_=false;
};
}} // namespace zseries::oem
