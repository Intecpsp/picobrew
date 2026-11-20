// Z119 HttpUpdate and DownloadBinary HTTP receiver bodies.
#pragma once
#include "zseries/console.hpp"
#include "zseries/http_client.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {
enum class DownloadResult { Ok, MissingService, ServiceFailure, InvalidSpan };
enum class UpdateOperation : std::uint8_t { Begin, Write, End, IsFinished, Error, Md5, Restart };
struct UpdateAction {
    UpdateOperation operation;
    std::uint32_t argument = 0;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    char* text_output = nullptr;
    explicit UpdateAction(UpdateOperation value) noexcept : operation(value) {}
};
enum class UpdateValue : std::uint8_t { ElapsedSeconds, ErrorCode, StateComplete, Md5 };
struct FirmwareUpdateServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    bool (*perform)(void*, const UpdateAction&, std::uint32_t& result) = nullptr;
    bool (*value)(void*, UpdateValue, std::uint32_t number, const char* text) = nullptr;
    ConsoleServices diagnostics{};
};
class FirmwareUpdate {
public:
    FirmwareUpdate() = default;
    FirmwareUpdate(const FirmwareUpdate&) = delete;
    FirmwareUpdate& operator=(const FirmwareUpdate&) = delete;
    FirmwareUpdateServices services;
    bool completed = false, error = false;
    std::uint32_t received = 0, expected = 0, started_ms = 0;
    DownloadResult callback_result = DownloadResult::Ok;
    // The owning Core clears these two flags before configuring a new request.
    void reset_status() noexcept { completed = false; error = false; callback_result = DownloadResult::Ok; }
    DownloadResult headers(const HttpResponse&, bool& accepted);
    DownloadResult body(const std::uint8_t*, std::size_t);
    DownloadResult complete();
    HttpReceiver receiver() noexcept;
private:
    bool available() const noexcept;
    DownloadResult operation(const UpdateAction&, std::uint32_t* result = nullptr);
    DownloadResult diagnostic(std::uint32_t line, const char* message);
};
class BinaryDownload {
public:
    BinaryDownload() = default;
    BinaryDownload(const BinaryDownload&) = delete;
    BinaryDownload& operator=(const BinaryDownload&) = delete;
    ConsoleServices services;
    std::array<std::uint8_t, 2048> data{};
    std::uint32_t received = 0, expected = 0;
    bool completed = false, error = false;
    DownloadResult callback_result = DownloadResult::Ok;
    void reset_status() noexcept { completed = false; error = false; callback_result = DownloadResult::Ok; }
    DownloadResult headers(const HttpResponse&, bool& accepted);
    DownloadResult body(const std::uint8_t*, std::size_t);
    DownloadResult complete();
    HttpReceiver receiver() noexcept;
private:
    DownloadResult diagnostic(std::uint32_t line, const char* message);
};
}} // namespace zseries::oem
