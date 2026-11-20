// Complete original 0x40110f80 live HTTP diagnostic and response Stream.
#pragma once
#include "zseries/http_client.hpp"
#include "zseries/application_console.hpp"

namespace zseries { namespace oem {
struct HttpClientTestResources {
    void* context = nullptr;
    // Resource bytes belong to the preserved OEM image. The concrete build
    // materializes them privately; source and proof fixtures use addresses or
    // hashes, never copies of embedded tokens or token-bearing assertions.
    const char* (*text)(void*, std::uint32_t original_address) = nullptr;
};
ConsoleResult http_client_console_test(HttpServices, const ConsoleServices&,
    const ApplicationConsoleHooks&, const HttpClientTestResources&);
}} // namespace zseries::oem
