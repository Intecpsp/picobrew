#include "http_download_platform.hpp"
#include <Update.h>
#include <cstring>

namespace zseries { namespace oem { namespace esp32 {
namespace {
std::uint32_t clock(void*) { return ::millis(); }
bool perform(void* opaque, const UpdateAction& action, std::uint32_t& result) {
    auto& binding = *static_cast<FirmwareUpdateBindings*>(opaque);
    if (!binding.update) return false;
    auto& update = *binding.update; result = 0;
    switch (action.operation) {
    case UpdateOperation::Begin: result = update.begin(action.argument, U_FLASH, -1); break;
    // The historical API takes a mutable pointer but reads the supplied bytes.
    case UpdateOperation::Write: result = update.write(const_cast<std::uint8_t*>(action.data), action.size); break;
    case UpdateOperation::End: result = update.end(false); break;
    case UpdateOperation::IsFinished: result = update.isFinished(); break;
    case UpdateOperation::Error: result = update.getError(); break;
    case UpdateOperation::Md5: {
        if (!action.text_output || action.size < 33) return false;
        const auto value = update.md5String();
        if (value.length() >= action.size) return false;
        std::memcpy(action.text_output, value.c_str(), value.length() + 1); break;
    }
    case UpdateOperation::Restart: ESP.restart(); break;
    }
    return true;
}
bool value(void* opaque, UpdateValue kind, std::uint32_t number, const char* text) {
    auto& binding = *static_cast<FirmwareUpdateBindings*>(opaque);
    if (!binding.output) return false;
    const auto seconds = static_cast<double>(static_cast<float>(::millis()) / 1000.0f);
    switch (kind) {
    case UpdateValue::ElapsedSeconds:
        return binding.output->printf("[IV][%8.2f][OTA] Ellapsed = %lu s\r\n", seconds, static_cast<unsigned long>(number)) > 0;
    case UpdateValue::ErrorCode:
        return binding.output->printf("[IV][%8.2f][OTA] ERROR = %lu\r\n", seconds, static_cast<unsigned long>(number)) > 0;
    case UpdateValue::StateComplete:
        return binding.output->printf("[IV][%8.2f][OTA] STATE = %s\r\n", seconds, text) > 0;
    case UpdateValue::Md5:
        return binding.output->printf("[IV][%8.2f][OTA] MD5 = %s\r\n", seconds, text) > 0;
    }
    return false;
}
}
FirmwareUpdateServices firmware_update_services(FirmwareUpdateBindings& binding) noexcept {
    FirmwareUpdateServices services; services.context = &binding; services.millis = clock;
    if (binding.update) services.perform = perform;
    if (binding.output) services.value = value;
    services.diagnostics = binding.diagnostics; return services;
}
}}} // namespace zseries::oem::esp32
