#include "picobrew/provisioning.hpp"
#include "records.hpp"

namespace picobrew { namespace standalone {
using namespace detail;
namespace {
bool valid_key(const char* key, std::size_t length) {
    if (length != 64) return false;
    for (unsigned i = 0; i < 64; ++i)
        if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f'))) return false;
    return true;
}
bool valid_network(const NetworkCredentials& network) {
    const char* ssid_end = static_cast<const char*>(std::memchr(network.ssid, 0, sizeof(network.ssid)));
    const char* pass_end = static_cast<const char*>(std::memchr(network.password, 0, sizeof(network.password)));
    if (!ssid_end || ssid_end == network.ssid || !pass_end) return false;
    for (const char* p = network.ssid; p < ssid_end; ++p) if (static_cast<unsigned char>(*p) < 32 || *p == 127) return false;
    const std::size_t length = pass_end - network.password;
    if (length && (length < 8 || length > 64)) return false;
    for (const char* p = network.password; p < pass_end; ++p) {
        const unsigned char c = *p;
        if (c < 32 || c > 126) return false;
        if (length == 64 && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}
Result io_error() { return Result(503, "storage_io", "ERROR: saved provisioning could not be verified"); }
Result invalid() { return Result(400, "invalid_provisioning", "ERROR: use key <64 lowercase hex>, wifi <SSID><TAB><password>, or status"); }
}
Result Provisioning::load() {
    loaded_ = true; network_ = NetworkCredentials(); key_[0] = 0;
    network_generation_ = 0; network_changed_ = key_changed_ = false;
    if (!storage_.read || !storage_.write_atomic) return io_error();
    Result result;
    std::uint8_t bytes[99] = {}; std::size_t copied = 0, total = 0;
    const StorageResult key_status = storage_.read(storage_.context, "api.key", 0, bytes, 67, copied, total);
    if (key_status == StorageResult::Ok) {
        const bool ending = (total == 64) || (total == 65 && bytes[64] == '\n') ||
            (total == 66 && bytes[64] == '\r' && bytes[65] == '\n');
        if (copied != total || !ending || !valid_key(reinterpret_cast<const char*>(bytes), 64)) result = io_error();
        else { std::memcpy(key_, bytes, 64); key_[64] = 0; }
    } else if (key_status != StorageResult::Missing) result = io_error();
    Records records(storage_); std::size_t length = 0;
    const RecordResult network_status = records.load("network", bytes, sizeof(bytes), length, network_generation_);
    network_exists_ = network_status != RecordResult::Missing;
    if (network_status == RecordResult::Ok) {
        if (length != sizeof(bytes) || bytes[0] != 1) return io_error();
        NetworkCredentials stored; std::memcpy(stored.ssid, bytes + 1, 33); std::memcpy(stored.password, bytes + 34, 65);
        if (!valid_network(stored)) return io_error();
        network_ = stored;
    } else if (network_status != RecordResult::Missing) return io_error();
    return result;
}
Result Provisioning::save_key(const char* value) {
    if (storage_.write_atomic(storage_.context, "api.key", reinterpret_cast<const std::uint8_t*>(value), 64) != StorageResult::Ok) return io_error();
    std::uint8_t verified[65]; std::size_t count = 0, total = 0;
    if (storage_.read(storage_.context, "api.key", 0, verified, sizeof(verified), count, total) != StorageResult::Ok ||
        count != 64 || total != 64 || std::memcmp(verified, value, 64)) return io_error();
    std::memcpy(key_, value, 64); key_[64] = 0; key_changed_ = true;
    return Result(200, nullptr, "OK: API key saved");
}
Result Provisioning::save_network(const NetworkCredentials& value) {
    if (!valid_network(value)) return invalid();
    std::uint8_t bytes[99] = {}; bytes[0] = 1;
    std::memcpy(bytes + 1, value.ssid, 33); std::memcpy(bytes + 34, value.password, 65);
    Records records(storage_); std::uint32_t generation = 0;
    if (records.save("network", bytes, sizeof(bytes), network_generation_, generation) != RecordResult::Ok) return io_error();
    network_ = value; network_generation_ = generation; network_exists_ = network_changed_ = true;
    return Result(200, nullptr, "OK: network credentials saved");
}
Result Provisioning::import_network(const NetworkCredentials& value) {
    if (!loaded_) return Result(503, "not_ready", "ERROR: provisioning has not been loaded");
    if (network_exists_) return Result(409, "already_configured", "Modified network state already exists");
    if (!idle_ || !idle_(context_)) return Result(409, "busy", "ERROR: stop the appliance before changing provisioning");
    return save_network(value);
}
bool Provisioning::take_network_changed() noexcept { const bool changed = network_changed_; network_changed_ = false; return changed; }
bool Provisioning::take_key_changed() noexcept { const bool changed = key_changed_; key_changed_ = false; return changed; }
Result Provisioning::execute(const char* line, std::size_t length) {
    if (!loaded_ || !storage_.read || !storage_.write_atomic) return Result(503, "not_ready", "ERROR: provisioning storage is unavailable");
    if (!line || !length || length >= sizeof(line_) || std::memchr(line, 0, length)) return invalid();
    if (length == 6 && !std::memcmp(line, "status", 6)) {
        const char* message = network_configured() ? (key_configured() ? "Provisioned: network and API key" : "Provisioned: network; API key missing") :
            (key_configured() ? "Provisioned: API key; network missing" : "Provisioned: neither network nor API key");
        return Result(200, nullptr, message);
    }
    if (!idle_ || !idle_(context_)) return Result(409, "busy", "ERROR: stop the appliance before changing provisioning");
    if (length == 68 && !std::memcmp(line, "key ", 4) && valid_key(line + 4, 64)) return save_key(line + 4);
    if (length > 6 && !std::memcmp(line, "wifi ", 5)) {
        const char* tab = static_cast<const char*>(std::memchr(line + 5, '\t', length - 5));
        if (!tab || tab == line + 5 || tab - line - 5 > 32 || length - (tab - line) - 1 > 64) return invalid();
        NetworkCredentials credentials;
        std::memcpy(credentials.ssid, line + 5, tab - line - 5);
        std::memcpy(credentials.password, tab + 1, length - (tab - line) - 1);
        return save_network(credentials);
    }
    return invalid();
}
bool Provisioning::feed(char byte, Result& result) {
    if (byte == '\n') {
        if (!discarded_ && line_length_ && line_[line_length_ - 1] == '\r') --line_length_;
        if (!discarded_ && !line_length_) { std::memset(line_, 0, sizeof(line_)); return false; }
        result = discarded_ ? invalid() : execute(line_, line_length_);
        std::memset(line_, 0, sizeof(line_)); line_length_ = 0; discarded_ = false; return true;
    }
    if (discarded_) return false;
    if (!byte || line_length_ + 1 >= sizeof(line_)) {
        discarded_ = true; std::memset(line_, 0, sizeof(line_)); line_length_ = 0; return false;
    }
    line_[line_length_++] = byte; return false;
}
}}
