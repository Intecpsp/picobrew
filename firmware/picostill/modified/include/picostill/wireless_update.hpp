#pragma once
#include "picobrew/http_server.h"
#include <cstdint>
#include <cstddef>

namespace picostill { namespace update {

constexpr std::uint32_t SlotBytes = 0xf0000;
constexpr std::size_t ChunkBytes = 1024;
#define PICOSTILL_VERSION "1.2.7"
constexpr const char* Version = PICOSTILL_VERSION;
constexpr const char* Protocol = "esp32-factory-v1";
constexpr const char* IdentityPrefix = "PicoBrew OTA1|picostill|";
enum class Role { Unavailable, Application, Recovery, InvalidRecovery };

class Flash {
public:
    virtual Role role() const = 0;
    virtual const char* running_sha256() const = 0;
    virtual std::uint32_t running_size() const = 0;
    virtual bool select_recovery() = 0;
    virtual bool start(std::uint32_t size) = 0;
    virtual void abandon() = 0;
    virtual std::uint32_t session_id() = 0;
    virtual bool write(const std::uint8_t* bytes, std::size_t size) = 0;
    virtual bool matches(std::uint32_t offset, const std::uint8_t* bytes, std::size_t size) = 0;
    virtual bool finish(std::uint32_t size, const char* version, const char* sha256) = 0;
};

class Service {
public:
    explicit Service(Flash& flash) : flash_(flash) {}
    // False delegates a normal application request to the brewer's existing API.
    bool handle(const PicoHttpRequest&, PicoHttpResponse&, char* arena, std::size_t capacity,
                const char* uid, bool idle);
    bool reboot_pending() const { return reboot_; }
private:
    Flash& flash_;
    bool reboot_ = false;
    std::uint32_t session_ = 0, offset_ = 0, size_ = 0;
    char version_[32] = {}, digest_[65] = {};
    void clear();
};
}}
