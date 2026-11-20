// Original PicoStill.h HTTP client and response protocol.
#pragma once
#include "zseries/pico_still.hpp"
#include "zseries/pico_still_sensors.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace zseries { namespace oem {
struct StillHttpResponse {
    std::int32_t status = 0, content_length = -1;
    std::array<char,65> content_type{};
};
struct StillHttpObserved { std::uint32_t state = 0, error = 0, socket_error = 0; };
enum class StillHttpEffect : std::uint8_t { Reset, Cancel, Configure, Get, Post };
struct StillHttpAction {
    StillHttpEffect effect;
    const char* uri = nullptr;
    const char* content_type = nullptr;
    const char* body = nullptr;
    std::size_t body_length = 0;
    std::uint32_t client_class = 2;
    explicit StillHttpAction(StillHttpEffect value) noexcept : effect(value) {}
};
struct StillHttpServices {
    void* context = nullptr;
    bool (*perform)(void*,const StillHttpAction&,std::uint32_t& result) = nullptr;
};
enum class StillClientResult { Ok, MissingService, ServiceFailure, InvalidCredentialsEncoding, InvalidBody };
// Original encoder 0x400e1180. Failure leaves the written prefix unchanged;
// callers must not treat a failed encoding as a terminated C string.
std::int32_t still_form_encode(char* destination,std::size_t capacity,const char* source) noexcept;
// Original 0x401bf4c8 / 0x400e7078; connectivity errors take text precedence.
bool still_has_error(const PicoStillState&) noexcept;
const char* still_error_text(const PicoStillState&,std::uint32_t& assertion_line) noexcept;
class StillClient {
public:
    StillClient(PicoStillState& state,PicoStillSamples& samples) noexcept : still(state),samples(samples) {}
    PicoStillState& still;PicoStillSamples& samples;
    StillHttpObserved http;StillHttpServices services;StillHttpResponse response;
    Command pending = Command::Empty;
    bool error = false, retryable = false, response_done = false;
    std::array<char,17> host{};
    std::array<char,129> uri{};
    std::array<char,257> body{};
    std::size_t body_length = 0;
    bool host_truncated = false, uri_truncated = false, body_truncated = false;
    std::uint32_t assertion_line = 0;
    void set_host(const char*) noexcept;
    void build_uri(Command) noexcept;
    void mark_error() noexcept;
    void tick() noexcept;
    bool succeeded() const noexcept { return !error && response_done; }
    StillClientResult reset();
    StillClientResult command(Command);
    StillClientResult configure(const WifiCredentials&);
    bool receive_headers(const StillHttpResponse&) noexcept;
    StillClientResult receive_body(const void*,std::size_t) noexcept;
    void complete(std::uint32_t now) noexcept;
    void observe(PicoStillObserved&) const noexcept;
private:
    StillClientResult perform(const StillHttpAction&,std::uint32_t* result = nullptr);
    StillClientResult effect(StillHttpEffect,std::uint32_t* result = nullptr);
};
}} // namespace zseries::oem
