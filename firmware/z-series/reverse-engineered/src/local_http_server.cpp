#include "zseries/local_http_server.hpp"
#include <cstdio>
#include <cstring>

namespace zseries { namespace oem {
constexpr std::size_t LocalHttpServer::BufferCapacity;
namespace {
bool line_end_space(char byte) noexcept {
    return byte == '\t' || byte == '\n' || byte == '\r' || byte == ' ';
}
bool text(const LocalHttpTransport& io, const char* value) {
    return io.write(io.context, reinterpret_cast<const std::uint8_t*>(value), std::strlen(value));
}
bool number(const LocalHttpTransport& io, std::int32_t value) {
    char output[12];
    ::snprintf(output, sizeof output, "%ld", static_cast<long>(value));
    return text(io, output);
}
}

void LocalHttpServer::clear_buffer() noexcept {
    buffer.fill(0); length = 0; overflow = false; frozen = false;
}

LocalHttpResult LocalHttpServer::enable(bool value) {
    if (enabled == value) return LocalHttpResult::Ok;
    const auto& io = services.transport;
    if (!io.stop_listener || (value && !io.begin_listener)) return LocalHttpResult::MissingService;
    enabled = value;
    if (!io.stop_listener(io.context)) return LocalHttpResult::ServiceFailure;
    if (enabled && !io.begin_listener(io.context)) return LocalHttpResult::ServiceFailure;
    return LocalHttpResult::Ok;
}

LocalHttpResult LocalHttpServer::tick(std::uint32_t now) {
    if (!enabled) return LocalHttpResult::Ok;
    const auto& io = services.transport;
    if (!io.connected || !io.accept || !io.available || !io.read || !io.close)
        return LocalHttpResult::MissingService;
    if (!io.connected(io.context)) {
        if (!io.accept(io.context)) return LocalHttpResult::ServiceFailure;
        if (io.connected(io.context)) {
            deadline = timeout_ms + now;
            request_complete = false;
            clear_buffer(); parser.reset();
        }
        return LocalHttpResult::Ok;
    }
    // The original asserts if this second connection observation fails.
    if (!io.connected(io.context)) return LocalHttpResult::ServiceFailure;
    if (deadline < now)
        return io.close(io.context) ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;

    bool bad_request = false;
    while (io.available(io.context) != 0 && !request_complete) {
        if (bad_request) break;
        const auto value = static_cast<std::uint32_t>(io.read(io.context));
        if (value > 255)
            return io.close(io.context) ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;
        if (value != 0) {
            // The original assertion prevents appending a frozen response or
            // zero-copy JSON body. Return an integration failure at that edge.
            if (frozen) return LocalHttpResult::ServiceFailure;
            if (length == BufferCapacity) overflow = true;
            else { buffer[length++] = static_cast<char>(value); buffer[length] = 0; }
        }
        if (!parser.headers_done) {
            if (value == '\n') {
                while (length && line_end_space(buffer[length - 1])) buffer[--length] = 0;
                const auto result = parser.consume_line(buffer.data());
                if (result != LocalHttpResult::Ok) return result;
                clear_buffer();
                if (parser.headers_done) {
                    if (parser.request.content_length == 0) request_complete = true;
                    else if (parser.request.content_length > 2047) bad_request = true;
                }
            }
        } else if (parser.request.content_length <= length) request_complete = true;
    }
    if (!bad_request && !request_complete) return LocalHttpResult::Ok;

    LocalHttpResult result;
    if (bad_request) {
        const auto& response = services.response;
        if (!response.headers) result = LocalHttpResult::MissingService;
        else result = response.headers(response.context, 400, 0, nullptr)
            ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;
    } else if (parser.request.method == HttpMethod::POST) {
        if (!services.post) result = LocalHttpResult::MissingService;
        else result = services.post(services.application_context, *this)
            ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;
    } else result = route_local_http(parser.request, services.response);
    const bool closed = io.close(io.context);
    return result != LocalHttpResult::Ok ? result : closed ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;
}

LocalHttpResult write_local_http_headers(const LocalHttpTransport& io, std::uint32_t status,
                                        std::int32_t length, const char* content_type) {
    if (!io.write) return LocalHttpResult::MissingService;
    const char* reason = status == 200 ? "OK" : status == 400 ? "BAD REQUEST" :
                         status == 404 ? "NOT FOUND" : "ERROR";
    if (!text(io, "HTTP/1.0 ") || !number(io, static_cast<std::int32_t>(status)) ||
        !text(io, " ") || !text(io, reason) || !text(io, "\r\n"))
        return LocalHttpResult::ServiceFailure;
    if (length >= 0 && (!text(io, "Content-Length: ") || !number(io, length) || !text(io, "\r\n")))
        return LocalHttpResult::ServiceFailure;
    if (content_type && *content_type &&
        (!text(io, "Content-Type: ") || !text(io, content_type) || !text(io, "\r\n")))
        return LocalHttpResult::ServiceFailure;
    return text(io, "Connection: close\r\n\r\n") ? LocalHttpResult::Ok : LocalHttpResult::ServiceFailure;
}
}} // namespace zseries::oem
