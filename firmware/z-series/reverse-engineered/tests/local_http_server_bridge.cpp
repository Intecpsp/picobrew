#include "zseries/local_http_server.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
LocalHttpServer server;
bool connected_value = false, accepted = false, header_mode = false;
std::vector<std::uint32_t> input, trace;
std::vector<std::uint8_t> output, posted;
std::size_t position = 0;
void event(std::uint32_t op, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0) {
    trace.insert(trace.end(), {op, a, b, c});
}
bool connected(void*) { event(1, connected_value); return connected_value; }
bool accept(void*) { event(2, accepted); connected_value = accepted; return true; }
int available(void*) { const auto n = input.size() - position; event(3, n); return static_cast<int>(n); }
int read(void*) { const auto value = input.at(position++); event(4, value); return static_cast<int>(value); }
bool close(void*) { event(5); connected_value = false; return true; }
bool stop_listener(void*) { event(6); return true; }
bool begin_listener(void*) { event(7); return true; }
bool write(void*, const std::uint8_t* bytes, std::size_t size) {
    if (!header_mode) event(9, size);
    output.insert(output.end(), bytes, bytes + size); return true;
}
bool headers(void*, std::uint32_t status, std::int32_t size, const char* type) {
    unsigned mime = !type ? 0 : std::strcmp(type, "text/html") == 0 ? 1 :
        std::strcmp(type, "text/javascript") == 0 ? 2 : std::strcmp(type, "text/css") == 0 ? 3 : 4;
    event(8, status, static_cast<std::uint32_t>(size), mime); return true;
}
bool post(void*, LocalHttpServer& value) {
    event(10, value.length);
    posted.insert(posted.end(), value.buffer.begin(), value.buffer.begin() + value.length); return true;
}
bool state(void*) { event(11); return true; }
std::int32_t decimal(void*, const char* text) { return static_cast<std::int32_t>(std::strtol(text, nullptr, 10)); }
}
extern "C" {
void http_server_initialize(const std::uint32_t* values, const char* path, const char* buffer) {
    server = LocalHttpServer();
    server.enabled = values[0]; server.deadline = values[1]; server.request_complete = values[2];
    server.length = std::strlen(buffer); std::memcpy(server.buffer.data(), buffer, server.length + 1);
    server.overflow = values[5]; server.frozen = values[6];
    auto& parser = server.parser;
    parser.request.method = static_cast<HttpMethod>(values[7]);
    parser.request.content_length = values[8]; parser.request.content_is_json = values[9];
    parser.request_line_seen = values[10]; parser.headers_done = values[11];
    parser.request.path.length = std::strlen(path);
    std::memcpy(parser.request.path.bytes.data(), path, parser.request.path.length + 1);
    parser.request.path.overflow = values[14]; connected_value = values[15];
    parser.services.decimal_integer = decimal;
    auto& io = server.services.transport;
    io.connected = connected; io.accept = accept; io.available = available; io.read = read;
    io.close = close; io.stop_listener = stop_listener; io.begin_listener = begin_listener; io.write = write;
    auto& response = server.services.response;
    response.headers = headers; response.write = write; response.state = state;
    server.services.post = post;
    trace.clear(); output.clear(); posted.clear(); header_mode = false;
}
int http_server_step(unsigned op, std::uint32_t argument, const std::uint32_t* bytes,
                     std::size_t count, bool accept_next, int connection) {
    input.assign(bytes, bytes + count); position = 0; accepted = accept_next;
    if (connection >= 0) connected_value = connection != 0;
    return static_cast<int>(op == 0 ? server.tick(argument) : server.enable(argument != 0));
}
int http_server_headers(std::uint32_t status, std::int32_t length, const char* type) {
    header_mode = true;
    return static_cast<int>(write_local_http_headers(server.services.transport, status, length, type));
}
void http_server_values(std::uint32_t* values) {
    const auto& parser = server.parser;
    const std::uint32_t state[] = {server.enabled, server.deadline, server.request_complete,
        server.length, 2048 - server.length, server.overflow, server.frozen,
        static_cast<std::uint32_t>(parser.request.method), parser.request.content_length,
        parser.request.content_is_json, parser.request_line_seen, parser.headers_done,
        parser.request.path.length, 64 - parser.request.path.length, parser.request.path.overflow, connected_value};
    std::copy(state, state + 16, values);
}
const char* http_server_buffer() { return server.buffer.data(); }
const char* http_server_path() { return server.parser.request.path.c_str(); }
std::size_t http_server_bytes(unsigned kind, std::uint8_t* bytes, std::size_t capacity) {
    const auto& values = kind == 0 ? output : posted;
    if (values.size() > capacity) return capacity + 1;
    std::copy(values.begin(), values.end(), bytes); return values.size();
}
std::size_t http_server_trace(std::uint32_t* bytes, std::size_t capacity) {
    if (trace.size() > capacity) return capacity + 1;
    std::copy(trace.begin(), trace.end(), bytes); return trace.size();
}
}
