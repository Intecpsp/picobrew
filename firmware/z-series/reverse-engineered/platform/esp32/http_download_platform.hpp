#pragma once
#include "zseries/http_download.hpp"
#include <Arduino.h>
class UpdateClass;

namespace zseries { namespace oem { namespace esp32 {
struct FirmwareUpdateBindings {
    UpdateClass* update = nullptr;
    Print* output = nullptr;
    ConsoleServices diagnostics{};
};
FirmwareUpdateServices firmware_update_services(FirmwareUpdateBindings&) noexcept;
}}} // namespace zseries::oem::esp32
