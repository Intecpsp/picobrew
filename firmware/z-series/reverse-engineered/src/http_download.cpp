#include "zseries/http_download.hpp"
#include <cstring>
#include <limits>

namespace zseries { namespace oem {
namespace {
bool firmware_headers(void* context, const HttpResponse& response) {
    auto& update = *static_cast<FirmwareUpdate*>(context); bool accepted = false;
    update.callback_result = update.headers(response, accepted);
    return update.callback_result == DownloadResult::Ok && accepted;
}
void firmware_body(void* context, const std::uint8_t* data, std::size_t size) {
    auto& update = *static_cast<FirmwareUpdate*>(context);
    if (update.callback_result == DownloadResult::Ok) update.callback_result = update.body(data, size);
}
void firmware_complete(void* context) {
    auto& update = *static_cast<FirmwareUpdate*>(context);
    if (update.callback_result == DownloadResult::Ok) update.callback_result = update.complete();
}
bool binary_headers(void* context, const HttpResponse& response) {
    auto& download = *static_cast<BinaryDownload*>(context); bool accepted = false;
    download.callback_result = download.headers(response, accepted);
    return download.callback_result == DownloadResult::Ok && accepted;
}
void binary_body(void* context, const std::uint8_t* data, std::size_t size) {
    auto& download = *static_cast<BinaryDownload*>(context);
    if (download.callback_result == DownloadResult::Ok) download.callback_result = download.body(data, size);
}
void binary_complete(void* context) {
    auto& download = *static_cast<BinaryDownload*>(context);
    if (download.callback_result == DownloadResult::Ok) download.callback_result = download.complete();
}
}
bool FirmwareUpdate::available() const noexcept {
    return services.millis && services.perform && services.value && services.diagnostics.log;
}
DownloadResult FirmwareUpdate::operation(const UpdateAction& action, std::uint32_t* result) {
    std::uint32_t value = 0;
    if (!services.perform(services.context, action, value)) return DownloadResult::ServiceFailure;
    if (result) *result = value;
    return DownloadResult::Ok;
}
DownloadResult FirmwareUpdate::diagnostic(std::uint32_t line, const char* message) {
    return services.diagnostics.log(services.diagnostics.context, ConsoleLevel::Error,
        "OTA", "HttpUpdate.h", line, message) ? DownloadResult::Ok : DownloadResult::ServiceFailure;
}
DownloadResult FirmwareUpdate::headers(const HttpResponse& response, bool& accepted) {
    accepted = false;
    if (!available()) return DownloadResult::MissingService;
    if (std::strcmp(response.content_type.data(), "application/octet-stream")) {
        const auto result = diagnostic(67, "Unexpected content type");
        if (result != DownloadResult::Ok) return result;
        error = true;
    } else {
        UpdateAction action(UpdateOperation::Begin);
        action.argument = static_cast<std::uint32_t>(response.content_length);
        std::uint32_t begun = 0; const auto result = operation(action, &begun);
        if (result != DownloadResult::Ok) return result;
        if (!begun) {
            const auto diagnosed = diagnostic(70, "Not enough space to begin OTA");
            if (diagnosed != DownloadResult::Ok) return diagnosed;
            error = true;
        } else started_ms = services.millis(services.context);
    }
    expected = static_cast<std::uint32_t>(response.content_length); received = 0;
    accepted = !error; return DownloadResult::Ok;
}
DownloadResult FirmwareUpdate::body(const std::uint8_t* bytes, std::size_t size) {
    if (!available()) return DownloadResult::MissingService;
    if ((!bytes && size) || size > std::numeric_limits<std::uint32_t>::max()) return DownloadResult::InvalidSpan;
    UpdateAction action(UpdateOperation::Write); action.data = bytes; action.size = size;
    const auto result = operation(action);
    if (result != DownloadResult::Ok) return result;
    // The OEM increments by the supplied length, even if Update.write returns
    // a smaller amount. Update.end/getError determine final SDK failure.
    received += static_cast<std::uint32_t>(size); return DownloadResult::Ok;
}
DownloadResult FirmwareUpdate::complete() {
    if (!available()) return DownloadResult::MissingService;
    const auto elapsed = (services.millis(services.context) - started_ms) / 1000;
    if (!services.value(services.context, UpdateValue::ElapsedSeconds, elapsed, nullptr)) return DownloadResult::ServiceFailure;
    std::uint32_t ended = 0; auto result = operation(UpdateAction(UpdateOperation::End), &ended);
    if (result != DownloadResult::Ok) return result;
    if (!ended) {
        result = diagnostic(93, "OTA Update.end() failed");
        if (result != DownloadResult::Ok) return result;
    } else {
        std::uint32_t finished = 0; result = operation(UpdateAction(UpdateOperation::IsFinished), &finished);
        if (result != DownloadResult::Ok) return result;
        if (finished) {
            if (!services.value(services.context, UpdateValue::StateComplete, 0, "COMPLETE")) return DownloadResult::ServiceFailure;
            char md5[33] = {}; UpdateAction action(UpdateOperation::Md5); action.text_output = md5; action.size = sizeof(md5);
            result = operation(action); if (result != DownloadResult::Ok) return result;
            md5[32] = 0;
            if (!services.value(services.context, UpdateValue::Md5, 0, md5)) return DownloadResult::ServiceFailure;
            result = operation(UpdateAction(UpdateOperation::Restart));
            if (result != DownloadResult::Ok) return result;
            completed = true; return DownloadResult::Ok;
        }
        result = diagnostic(100, "Update.isFinished() failed");
        if (result != DownloadResult::Ok) return result;
    }
    std::uint32_t code = 0; result = operation(UpdateAction(UpdateOperation::Error), &code);
    if (result != DownloadResult::Ok) return result;
    if (!services.value(services.context, UpdateValue::ErrorCode, code, nullptr)) return DownloadResult::ServiceFailure;
    if (!ended) { result = diagnostic(95, "Update Error"); if (result != DownloadResult::Ok) return result; }
    error = true; completed = true; return DownloadResult::Ok;
}
HttpReceiver FirmwareUpdate::receiver() noexcept {
    HttpReceiver result; result.context = this; result.headers = firmware_headers;
    result.body = firmware_body; result.complete = firmware_complete; return result;
}
DownloadResult BinaryDownload::diagnostic(std::uint32_t line, const char* message) {
    return services.log(services.context, ConsoleLevel::Error, "DBIN", "DownloadBinary.h", line, message) ?
        DownloadResult::Ok : DownloadResult::ServiceFailure;
}
DownloadResult BinaryDownload::headers(const HttpResponse& response, bool& accepted) {
    accepted = false;
    if (!services.log || !services.assertion) return DownloadResult::MissingService;
    received = 0; expected = static_cast<std::uint32_t>(response.content_length);
    if (std::strcmp(response.content_type.data(), "application/octet-stream") || response.content_length > 2048) {
        const bool type = std::strcmp(response.content_type.data(), "application/octet-stream") != 0;
        const auto result = diagnostic(type ? 67 : 70, type ? "Unexpected content type" : "Not enough space to begin OTA");
        if (result != DownloadResult::Ok) return result;
        error = true;
    }
    // The OEM signed comparison accepts a negative/unknown Content-Length.
    // That behavior is recorded here; a modified update route must validate it.
    accepted = !error; return DownloadResult::Ok;
}
DownloadResult BinaryDownload::body(const std::uint8_t* bytes, std::size_t size) {
    if (!services.log || !services.assertion) return DownloadResult::MissingService;
    if ((!bytes && size) || size > std::numeric_limits<std::uint32_t>::max()) return DownloadResult::InvalidSpan;
    if (received >= data.size() && !services.assertion(services.context, "DownloadBinary.h", 84, "_ibuf < sizeof(_buf)"))
        return DownloadResult::ServiceFailure;
    // An already-corrupt index would underflow the OEM remaining count and
    // permit an out-of-bounds copy. Reject that domain after its assertion.
    if (received > data.size()) return DownloadResult::InvalidSpan;
    if (size > data.size() - received) {
        const auto result = diagnostic(87, "Internal buffer too short");
        if (result != DownloadResult::Ok) return result;
        error = true; return DownloadResult::Ok;
    }
    if (size) std::memcpy(data.data() + received, bytes, size);
    received += static_cast<std::uint32_t>(size); return DownloadResult::Ok;
}
DownloadResult BinaryDownload::complete() {
    if (!services.log || !services.assertion) return DownloadResult::MissingService;
    if (received != expected && !services.assertion(services.context, "DownloadBinary.h", 78, "_ibuf == _contentLength"))
        return DownloadResult::ServiceFailure;
    completed = true; return DownloadResult::Ok;
}
HttpReceiver BinaryDownload::receiver() noexcept {
    HttpReceiver result; result.context = this; result.headers = binary_headers;
    result.body = binary_body; result.complete = binary_complete; return result;
}
}} // namespace zseries::oem
