// Local HttpParser.h / HttpServer.h behavior recovered from Z119.
#pragma once
#include "zseries/enums.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {

struct LocalHttpPath {
    static constexpr std::size_t Capacity = 64;
    std::array<char, Capacity + 1> bytes{};
    std::uint32_t length = 0;
    bool overflow = false;
    const char* c_str() const noexcept { return bytes.data(); }
};

struct LocalHttpRequest {
    HttpMethod method = HttpMethod::Unknown;
    std::uint32_t content_length = 0;
    bool content_is_json = false;
    LocalHttpPath path;
};

struct LocalHttpParserServices {
    void* context = nullptr;
    // The original calls ROM atoi at 0x400566c4. This is a library boundary,
    // not a second copy of the application parser or an HTTP validation rule.
    std::int32_t (*decimal_integer)(void*, const char*) = nullptr;
};

enum class LocalHttpResult { Ok, MissingService, ServiceFailure };

class LocalHttpParser {
public:
    LocalHttpParserServices services;
    LocalHttpRequest request;
    bool request_line_seen = false;
    bool headers_done = false;

    // 0x400eeffc; retains the service binding, as the OEM retains its logger.
    void reset() noexcept;
    // 0x40109b4c; malformed lines can leave an earlier method or path intact.
    void parse_request_line(const char*) noexcept;
    // 0x40109c34; callers supply NUL-terminated lines after transport trimming.
    // This intentionally preserves case-sensitive/prefix matching and does not
    // turn the recovered OEM parser into a standards-compliant HTTP validator.
    LocalHttpResult consume_line(const char*) noexcept;
};

struct LocalHttpAsset {
    const char* name;
    const char* content_type;
    const std::uint8_t* bytes;
    std::size_t size;
};
constexpr std::size_t LocalHttpAssetCount = 8;
const LocalHttpAsset* local_http_asset(std::size_t index) noexcept;

struct LocalHttpResponseServices {
    void* context = nullptr;
    // 0x400d97c4: response-header formatting and socket writes remain services.
    bool (*headers)(void*, std::uint32_t status, std::int32_t length,
                    const char* content_type) = nullptr;
    bool (*write)(void*, const std::uint8_t*, std::size_t) = nullptr;
    // 0x4011ae84: serializes the live machine state; no placeholder JSON body.
    bool (*state)(void*) = nullptr;
};

// 0x4011b0e4. The caller routes POST separately; this body itself ignores the
// method. Empty paths and '/' select '/z.html'. Paths are not URL-decoded,
// case-folded, or stripped of queries, and the asset loop skips byte zero.
LocalHttpResult route_local_http(const LocalHttpRequest&,
                                const LocalHttpResponseServices&);

}} // namespace zseries::oem
