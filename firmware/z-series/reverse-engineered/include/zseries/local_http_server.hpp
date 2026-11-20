// Incoming HTTP connection lifecycle and framing recovered from Z119.
#pragma once
#include "zseries/local_http.hpp"

namespace zseries { namespace oem {

struct LocalHttpTransport {
    void* context = nullptr;
    bool (*connected)(void*) = nullptr;
    // Assign the next nonblocking accept result to the current client. The
    // return value reports adapter completion; connected() reports acceptance.
    bool (*accept)(void*) = nullptr;
    int (*available)(void*) = nullptr;
    int (*read)(void*) = nullptr;
    bool (*close)(void*) = nullptr;
    bool (*stop_listener)(void*) = nullptr;
    bool (*begin_listener)(void*) = nullptr;
    bool (*write)(void*, const std::uint8_t*, std::size_t) = nullptr;
};

class LocalHttpServer;
struct LocalHttpServerServices {
    LocalHttpTransport transport;
    LocalHttpResponseServices response;
    void* application_context = nullptr;
    bool (*post)(void*, LocalHttpServer&) = nullptr;
};

class LocalHttpServer {
public:
    static constexpr std::size_t BufferCapacity = 2048;
    LocalHttpServerServices services;
    LocalHttpParser parser;
    std::array<char, BufferCapacity + 1> buffer{};
    std::uint32_t length = 0;
    bool overflow = false, frozen = false;
    bool enabled = false, request_complete = false;
    std::uint32_t timeout_ms = 1000, deadline = 0;

    void clear_buffer() noexcept;
    // 0x400d5c54: changing either direction stops the listener; enabling then
    // begins it. The current accepted client is not explicitly closed here.
    LocalHttpResult enable(bool);
    // 0x4011fc58. A newly accepted client starts reading on the next tick.
    // The OEM compares deadline < now directly, including uint32 wrap behavior.
    LocalHttpResult tick(std::uint32_t now);
};

// 0x400d97c4: exact HTTP/1.0 status/header bytes, including uppercase reasons.
// Negative content length and empty/null MIME types omit those header fields.
LocalHttpResult write_local_http_headers(const LocalHttpTransport&, std::uint32_t status,
                                        std::int32_t length, const char* content_type);

}} // namespace zseries::oem
