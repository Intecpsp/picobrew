#include "zseries/http_download.hpp"
#include "zseries/diagnostics.hpp"
#include <cstring>
#include <string>
#include <vector>
using namespace zseries::oem;
namespace {
std::vector<std::string> events;
struct Context { const std::uint32_t* input; std::uint32_t now; DiagnosticState diagnostics; };
std::uint32_t hash(const std::uint8_t* data, std::size_t size) {
    std::uint32_t value = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) value = (value ^ data[i]) * 16777619u;
    return value;
}
std::uint32_t clock(void* opaque) { return static_cast<Context*>(opaque)->now; }
void assertion_log(void* opaque, double seconds, const char* file, std::uint32_t line, const char* expression) {
    auto& context = *static_cast<Context*>(opaque); std::uint64_t bits; std::memcpy(&bits, &seconds, 8);
    events.push_back(std::string("A|") + file + "|" + std::to_string(line) + "|" + expression + "|" +
        std::to_string(static_cast<std::uint32_t>(bits)) + "|" + std::to_string(static_cast<std::uint32_t>(bits >> 32)) + "|" +
        std::to_string(context.diagnostics.assertion_count));
}
void delay_ms(void* opaque, std::uint32_t duration) {
    auto& context = *static_cast<Context*>(opaque);
    events.push_back("D|" + std::to_string(duration) + "|" + std::to_string(context.diagnostics.assertion_count)); context.now += duration;
}
bool assertion(void* opaque, const char* file, std::uint32_t line, const char* expression) {
    DiagnosticServices services; services.context = opaque; services.millis = clock;
    services.assertion_log = assertion_log; services.delay_ms = delay_ms;
    return record_assertion(static_cast<Context*>(opaque)->diagnostics, services, file, line, expression) == DiagnosticResult::Ok;
}
bool log(void*, ConsoleLevel, const char* tag, const char* file, std::uint32_t line, const char* message) {
    events.push_back(std::string("L|") + tag + "|" + file + "|" + std::to_string(line) + "|" + message); return true;
}
bool value(void*, UpdateValue kind, std::uint32_t number, const char* text) {
    events.push_back("V|" + std::to_string(static_cast<unsigned>(kind)) + "|" + std::to_string(number) + "|" + (text ? text : "")); return true;
}
bool perform(void* opaque, const UpdateAction& action, std::uint32_t& result) {
    const auto& context = *static_cast<Context*>(opaque); result = 0;
    switch (action.operation) {
    case UpdateOperation::Begin: events.push_back("P|0|" + std::to_string(action.argument) + "|0|4294967295"); result = context.input[8]; break;
    case UpdateOperation::Write: events.push_back("P|1|" + std::to_string(action.size) + "|" + std::to_string(hash(action.data, action.size))); result = context.input[11]; break;
    case UpdateOperation::End: events.push_back("P|2|0"); result = context.input[9]; break;
    case UpdateOperation::IsFinished: result = context.input[10]; break;
    case UpdateOperation::Error: result = context.input[12]; break;
    case UpdateOperation::Md5: events.push_back("P|5"); std::memcpy(action.text_output, "0123456789abcdef0123456789abcdef", 33); break;
    case UpdateOperation::Restart: events.push_back("P|6"); break;
    }
    return true;
}
}
extern "C" void http_download_case(const std::uint32_t* input, const char* type, unsigned size, std::uint32_t* output) {
    events.clear(); Context context; context.input = input; context.now = input[6]; context.diagnostics.assertion_count = input[7];
    ConsoleServices diagnostics; diagnostics.context = &context; diagnostics.assertion = assertion; diagnostics.log = log;
    FirmwareUpdate firmware; firmware.services.context = &context; firmware.services.millis = clock;
    firmware.services.perform = perform; firmware.services.value = value; firmware.services.diagnostics = diagnostics;
    firmware.received = input[1]; firmware.expected = input[2]; firmware.started_ms = input[3]; firmware.completed = input[4]; firmware.error = input[5];
    BinaryDownload binary; binary.services = diagnostics; binary.received = input[1]; binary.expected = input[2];
    binary.completed = input[4]; binary.error = input[5]; binary.data.fill(0xa5);
    HttpResponse response; response.content_length = static_cast<std::int32_t>(input[2]); std::strcpy(response.content_type.data(), type);
    std::vector<std::uint8_t> bytes(size); for (unsigned i = 0; i < size; ++i) bytes[i] = static_cast<std::uint8_t>(i * 13 + 7);
    bool accepted = false; DownloadResult result = DownloadResult::Ok;
    switch (input[0]) {
    case 0: result = firmware.headers(response, accepted); break;
    case 1: result = firmware.body(bytes.data(), bytes.size()); break;
    case 2: result = firmware.complete(); break;
    case 3: result = binary.headers(response, accepted); break;
    case 4: result = binary.body(bytes.data(), bytes.size()); break;
    case 5: result = binary.complete(); break;
    case 6:
        result = firmware.headers(response, accepted);
        if (accepted) { result = firmware.body(bytes.data(), bytes.size()); if (result == DownloadResult::Ok) result = firmware.complete(); }
        break;
    case 7:
        result = binary.headers(response, accepted);
        if (accepted) { result = binary.body(bytes.data(), bytes.size()); if (result == DownloadResult::Ok) result = binary.complete(); }
        break;
    }
    const bool is_binary = (input[0] >= 3 && input[0] <= 5) || input[0] == 7;
    output[0] = is_binary ? binary.received : firmware.received;
    output[1] = is_binary ? binary.expected : firmware.expected;
    output[2] = is_binary ? input[3] : firmware.started_ms;
    output[3] = is_binary ? binary.completed : firmware.completed;
    output[4] = is_binary ? binary.error : firmware.error;
    output[5] = accepted; output[6] = static_cast<unsigned>(result); output[7] = context.now;
    output[8] = context.diagnostics.assertion_count; output[9] = hash(binary.data.data(), binary.data.size());
}
extern "C" unsigned http_download_event_count() { return events.size(); }
extern "C" const char* http_download_event(unsigned index) { return events.at(index).c_str(); }
extern "C" unsigned http_download_guard(unsigned mode) {
    std::uint32_t input[13] = {}; input[8] = 1; input[9] = 1; input[10] = 1;
    Context context; context.input = input; context.now = 0;
    FirmwareUpdate firmware; BinaryDownload binary;
    ConsoleServices diagnostics; diagnostics.context = &context; diagnostics.assertion = assertion; diagnostics.log = log;
    if (!mode) return static_cast<unsigned>(firmware.complete());
    if (mode == 1) return static_cast<unsigned>(binary.complete());
    binary.services = diagnostics;
    if (mode == 2) {
        binary.received = 2049; const std::uint8_t byte = 42;
        return static_cast<unsigned>(binary.body(&byte, 1)) + context.diagnostics.assertion_count * 256;
    }
    if (mode == 3) return static_cast<unsigned>(binary.body(nullptr, 1));
    HttpResponse response; response.content_length = 2; std::strcpy(response.content_type.data(), "application/octet-stream");
    if (mode == 4) {
        const auto receiver = binary.receiver(); if (!receiver.headers(receiver.context, response)) return 1;
        std::uint8_t bytes[] = {3, 4}; receiver.body(receiver.context, bytes, 2); bytes[0] = 9;
        receiver.complete(receiver.context);
        return binary.completed && binary.data[0] == 3 && binary.data[1] == 4 && !binary.error ? 0 : 2;
    }
    firmware.services.context = &context; firmware.services.millis = clock; firmware.services.value = value;
    firmware.services.diagnostics = diagnostics;
    firmware.services.perform = [](void*, const UpdateAction&, std::uint32_t&) { return false; };
    const auto receiver = firmware.receiver(); const auto before = events.size();
    if (receiver.headers(receiver.context, response)) return 1;
    receiver.body(receiver.context, nullptr, 0); receiver.complete(receiver.context);
    return firmware.callback_result == DownloadResult::ServiceFailure && !firmware.completed && events.size() == before ? 0 : 2;
}
