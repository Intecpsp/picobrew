#include "zseries/local_http.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace zseries::oem;
namespace {
LocalHttpParser parser;
std::vector<std::uint32_t> trace;
std::vector<std::uint8_t> body;
std::int32_t decimal(void*, const char* input) {
    // Comparison inputs stay in the original signed-32-bit conversion domain.
    return static_cast<std::int32_t>(std::strtol(input, nullptr, 10));
}
bool headers(void*, std::uint32_t status, std::int32_t size, const char* type) {
    std::uint32_t kind = 0;
    if (type) {
        if (std::strcmp(type, "text/html") == 0) kind = 1;
        else if (std::strcmp(type, "text/javascript") == 0) kind = 2;
        else if (std::strcmp(type, "text/css") == 0) kind = 3;
        else return false;
    }
    trace.insert(trace.end(), {0, status, static_cast<std::uint32_t>(size), kind});
    return true;
}
bool write(void*, const std::uint8_t* data, std::size_t size) {
    trace.insert(trace.end(), {1, static_cast<std::uint32_t>(size), 0, 0});
    body.insert(body.end(), data, data + size);
    return true;
}
bool state(void*) { trace.insert(trace.end(), {2, 0, 0, 0}); return true; }
}

extern "C" {
void local_http_initialize(const std::uint32_t* values, const char* path) {
    parser = LocalHttpParser();
    parser.services.decimal_integer = decimal;
    parser.request.method = static_cast<HttpMethod>(values[0]);
    parser.request.content_length = values[1];
    parser.request.content_is_json = values[2] != 0;
    parser.request_line_seen = values[3] != 0;
    parser.headers_done = values[4] != 0;
    parser.request.path.length = static_cast<std::uint32_t>(std::strlen(path));
    std::memcpy(parser.request.path.bytes.data(), path, parser.request.path.length + 1);
    parser.request.path.overflow = values[5] != 0;
    trace.clear(); body.clear();
}
int local_http_operation(unsigned operation, const char* line) {
    switch (operation) {
    case 0: return static_cast<int>(parser.consume_line(line));
    case 1: parser.parse_request_line(line); return 0;
    case 2: parser.reset(); return 0;
    case 3: {
        LocalHttpResponseServices services;
        services.headers = headers; services.write = write; services.state = state;
        return static_cast<int>(route_local_http(parser.request, services));
    }
    default: return -1;
    }
}
const char* local_http_values(std::uint32_t* values) {
    values[0] = static_cast<std::uint32_t>(parser.request.method);
    values[1] = parser.request.content_length;
    values[2] = parser.request.content_is_json;
    values[3] = parser.request_line_seen;
    values[4] = parser.headers_done;
    values[5] = parser.request.path.length;
    values[6] = 64 - parser.request.path.length;
    values[7] = parser.request.path.overflow;
    return parser.request.path.c_str();
}
std::size_t local_http_trace(std::uint32_t* output, std::size_t capacity) {
    if (capacity < trace.size()) return capacity + 1;
    std::copy(trace.begin(), trace.end(), output);
    return trace.size();
}
std::size_t local_http_body(std::uint8_t* output, std::size_t capacity) {
    if (capacity < body.size()) return capacity + 1;
    std::copy(body.begin(), body.end(), output);
    return body.size();
}
}
