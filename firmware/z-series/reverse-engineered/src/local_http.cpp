#include "zseries/local_http.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
constexpr std::size_t LocalHttpPath::Capacity;
namespace {
bool empty(const char* text) noexcept { return !text || !*text; }
bool starts_with(const char* text, const char* prefix) noexcept {
    return text && std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}
void copy_path(LocalHttpPath& path, const char* first, std::size_t size) noexcept {
    path = LocalHttpPath();
    path.length = static_cast<std::uint32_t>(std::min(size, LocalHttpPath::Capacity));
    path.overflow = size > LocalHttpPath::Capacity;
    std::memcpy(path.bytes.data(), first, path.length);
}
LocalHttpResult body_response(const LocalHttpResponseServices& services,
                             const std::uint8_t* bytes, std::size_t size,
                             const char* content_type) {
    if (!services.headers || !services.write) return LocalHttpResult::MissingService;
    if (!services.headers(services.context, 200, static_cast<std::int32_t>(size), content_type))
        return LocalHttpResult::ServiceFailure;
    return services.write(services.context, bytes, size)
        ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;
}
}

void LocalHttpParser::reset() noexcept {
    request_line_seen = false;
    headers_done = false;
    request = LocalHttpRequest();
}

void LocalHttpParser::parse_request_line(const char* line) noexcept {
    if (!line) return;
    const char* first_space = std::strchr(line, ' ');
    if (!first_space) return;
    static const char* const methods[] = {"Unknown", "GET", "POST", "PUT", "DELETE", "UPDATE"};
    request.method = HttpMethod::Unknown;
    for (std::size_t index = 0; index < HttpMethodCount; ++index) {
        if (starts_with(line, methods[index])) {
            request.method = static_cast<HttpMethod>(index);
            break;
        }
    }
    const char* first = first_space + 1;
    const char* last = std::strchr(first, ' ');
    if (last && first < last) copy_path(request.path, first, static_cast<std::size_t>(last - first));
}

LocalHttpResult LocalHttpParser::consume_line(const char* line) noexcept {
    if (empty(line)) {
        headers_done = true;
        return LocalHttpResult::Ok;
    }
    if (!request_line_seen) {
        request_line_seen = true;
        parse_request_line(line);
    } else if (starts_with(line, "Content-Type: application/json")) {
        request.content_is_json = true;
    } else if (starts_with(line, "Content-Length: ")) {
        if (!services.decimal_integer) return LocalHttpResult::MissingService;
        request.content_length = static_cast<std::uint32_t>(services.decimal_integer(services.context, line + 16));
    }
    return LocalHttpResult::Ok;
}

LocalHttpResult route_local_http(const LocalHttpRequest& request,
                                const LocalHttpResponseServices& services) {
    const char* path = request.path.c_str();
    if (empty(path) || std::strcmp(path, "/") == 0) path = "/z.html";
    if (std::strcmp(path, "/ping") == 0) {
        static const std::uint8_t pong[] = {'P', 'O', 'N', 'G', '!'};
        return body_response(services, pong, sizeof pong, nullptr);
    }
    if (std::strcmp(path, "/state") == 0) {
        if (!services.state) return LocalHttpResult::MissingService;
        return services.state(services.context) ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;
    }
    for (std::size_t index = 0; index < LocalHttpAssetCount; ++index) {
        const LocalHttpAsset* asset = local_http_asset(index);
        if (std::strcmp(asset->name, path + 1) == 0)
            return body_response(services, asset->bytes, asset->size, asset->content_type);
    }
    if (!services.headers) return LocalHttpResult::MissingService;
    return services.headers(services.context, 404, 0, nullptr)
        ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;
}

}} // namespace zseries::oem
